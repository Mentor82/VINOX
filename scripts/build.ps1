[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$preset = "windows-msvc-$($Configuration.ToLowerInvariant())"
$cmake = "C:\Program Files\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\CMake\bin\ctest.exe"
$ninja = "C:\Qt\Tools\Ninja\ninja.exe"
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

foreach ($requiredPath in @($cmake, $ctest, $ninja, $vswhere)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required build tool not found: $requiredPath"
    }
}

$vsInstall = & $vswhere `
    -products Microsoft.VisualStudio.Product.BuildTools `
    -version "[17.0,18.0)" `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath `
    -latest
if (-not $vsInstall) {
    throw "Visual Studio Build Tools 2022 with C++ tools was not found"
}

$vsDevCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$env:PATH = "$machinePath;$userPath"

foreach ($name in @(
    "DevEnvDir",
    "FrameworkDir",
    "FrameworkVersion",
    "INCLUDE",
    "LIB",
    "LIBPATH",
    "UCRTVersion",
    "UniversalCRTSdkDir",
    "VCIDEInstallDir",
    "VCINSTALLDIR",
    "VCToolsInstallDir",
    "VCToolsRedistDir",
    "VCToolsVersion",
    "VSINSTALLDIR",
    "WindowsLibPath",
    "WindowsSdkBinPath",
    "WindowsSdkDir",
    "WindowsSDKLibVersion",
    "WindowsSDKVersion"
)) {
    Remove-Item -Path "Env:$name" -ErrorAction SilentlyContinue
}
Get-ChildItem Env: | Where-Object Name -Like "VSCMD_*" | Remove-Item

$environmentDump = & cmd.exe /d /s /c `
    "`"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 & set"
foreach ($line in $environmentDump) {
    if ($line -match "^([^=]+)=(.*)$") {
        Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
    }
}

if (-not $env:VCPKG_ROOT) {
    $bundledVcpkg = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg"
    if (-not (Test-Path -LiteralPath "$bundledVcpkg\vcpkg.exe")) {
        throw "Set VCPKG_ROOT to a vcpkg checkout or installation"
    }
    $env:VCPKG_ROOT = $bundledVcpkg
}

$buildRoot = Join-Path $projectRoot "out\$preset"
if ($Clean -and (Test-Path -LiteralPath $buildRoot)) {
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}

Push-Location $projectRoot
try {
    & $cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

    & $cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }

    & $ctest --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }

    & $cmake --install "out\$preset\build"
    if ($LASTEXITCODE -ne 0) { throw "Install failed" }

    & "out\$preset\stage\bin\vinox-cli.exe"
    if ($LASTEXITCODE -ne 0) { throw "Staged CLI smoke test failed" }

    $stageRoot = Join-Path $buildRoot "stage"
    $consumerBuild = Join-Path $buildRoot "package-consumer"
    & $cmake `
        -S "examples\package_consumer" `
        -B $consumerBuild `
        -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DCMAKE_BUILD_TYPE=$Configuration" `
        "-DCMAKE_PREFIX_PATH=$stageRoot"
    if ($LASTEXITCODE -ne 0) { throw "Package consumer configure failed" }

    & $cmake --build $consumerBuild
    if ($LASTEXITCODE -ne 0) { throw "Package consumer build failed" }

    $originalPath = $env:PATH
    try {
        $env:PATH = "$(Join-Path $stageRoot 'bin');$env:SystemRoot\System32"
        & "$consumerBuild\vinox_package_consumer.exe"
        if ($LASTEXITCODE -ne 0) { throw "Package consumer run failed" }
    }
    finally {
        $env:PATH = $originalPath
    }
}
finally {
    Pop-Location
}
