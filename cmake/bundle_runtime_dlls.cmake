# cmake/bundle_runtime_dlls.cmake
# Automatically collects and copies all standalone runtime dependencies
# (OpenVINO GenAI, oneTBB, vcpkg dependencies, and MSVC CRT) into the target directory.

if(NOT DEFINED DEST)
    message(FATAL_ERROR "DEST parameter is required")
endif()

string(REPLACE "\"" "" DEST "${DEST}")
file(MAKE_DIRECTORY "${DEST}")

# 1. OpenVINO Runtime & GenAI (Debug & Release) - prefer 2026.3.0, fallback to 2026.2.1
set(OV_SDK_PATHS
    "C:/ai/openvino_genai_2026.3.0/openvino_genai_windows_2026.3.0.0_x86_64"
    "C:/ai/openvino_genai_2026.2.1/openvino_genai_windows_2026.2.1.0_x86_64"
)
set(ACTIVE_OV_SDK "")
foreach(candidate IN LISTS OV_SDK_PATHS)
    if(EXISTS "${candidate}/runtime/bin/intel64/Release/openvino.dll")
        set(ACTIVE_OV_SDK "${candidate}")
        break()
    endif()
endforeach()

file(GLOB OPEN_VINO_DLLS
    "${ACTIVE_OV_SDK}/runtime/bin/intel64/Release/*.dll"
    "${ACTIVE_OV_SDK}/runtime/bin/intel64/Debug/*.dll"
)

# 2. oneTBB
file(GLOB TBB_DLLS
    "${ACTIVE_OV_SDK}/runtime/3rdparty/tbb/bin/*.dll"
)

# 3. vcpkg libraries (sqlite3, spdlog, fmt)
file(GLOB VCPKG_DLLS
    "${DEST}/../vcpkg_installed/x64-windows/bin/*.dll"
    "${DEST}/../vcpkg_installed/x64-windows/debug/bin/*.dll"
    "${DEST}/../../windows-msvc-debug/vcpkg_installed/x64-windows/bin/*.dll"
    "${DEST}/../../windows-msvc-debug/vcpkg_installed/x64-windows/debug/bin/*.dll"
    "${DEST}/../../windows-msvc-release/vcpkg_installed/x64-windows/bin/*.dll"
    "C:/ai/openvino/out/windows-msvc-debug/vcpkg_installed/x64-windows/bin/*.dll"
    "C:/ai/openvino/out/windows-msvc-debug/vcpkg_installed/x64-windows/debug/bin/*.dll"
    "C:/ai/openvino/out/windows-msvc-release/vcpkg_installed/x64-windows/bin/*.dll"
)

# 4. MSVC CRT (Redist & DebugCRT)
file(GLOB MSVC_CRT_DLLS
    "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Redist/MSVC/*/debug_nonredist/x64/Microsoft.VC*.DebugCRT/*.dll"
    "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT/*.dll"
)

# 5. MinGW C++ Runtime (for Qt GUI)
file(GLOB MINGW_DLLS
    "C:/Qt/Tools/mingw*/bin/libgcc_s*.dll"
    "C:/Qt/Tools/mingw*/bin/libstdc++*.dll"
    "C:/Qt/Tools/mingw*/bin/libwinpthread*.dll"
)

set(ALL_DLLS ${OPEN_VINO_DLLS} ${TBB_DLLS} ${VCPKG_DLLS} ${MSVC_CRT_DLLS} ${MINGW_DLLS})

set(COPIED_COUNT 0)
foreach(dll_file IN LISTS ALL_DLLS)
    get_filename_component(dll_name "${dll_file}" NAME)
    if(EXISTS "${dll_file}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${dll_file}" "${DEST}/${dll_name}"
            RESULT_VARIABLE res
        )
        if(res EQUAL 0)
            math(EXPR COPIED_COUNT "${COPIED_COUNT} + 1")
        endif()
    endif()
endforeach()

message(STATUS "[VINOX-BUNDLE] Synchronized ${COPIED_COUNT} standalone runtime DLLs into ${DEST}")
