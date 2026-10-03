@echo off
setlocal
cd /d "%~dp0"
set PATH=C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%~dp0out\windows-msvc-debug\build;%PATH%
set QML_IMPORT_PATH=%~dp0out\windows-msvc-debug\build\gui-qt6;%QML_IMPORT_PATH%
echo ==============================================================================
echo   Starting VINOX Desktop GUI
echo ==============================================================================
start "" "%~dp0out\windows-msvc-debug\build\vinox-gui.exe"
exit /b 0
