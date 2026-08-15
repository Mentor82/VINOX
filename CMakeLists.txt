cmake_minimum_required(VERSION 3.29)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
endif()

project(
    vinox
    VERSION 0.1.0
    DESCRIPTION "Standalone C++ GenAI infrastructure powered by OpenVINO"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)

set(
    VINOX_OPENVINO_SDK_ROOT
    ""
    CACHE PATH
    "Root of the OpenVINO GenAI distribution"
)

add_library(vinox_core SHARED
    src/core/version.cpp
)
add_library(vinox::core ALIAS vinox_core)
set_target_properties(vinox_core PROPERTIES EXPORT_NAME core)

target_compile_definitions(vinox_core
    PRIVATE
        VINOX_BUILD_SHARED
        VINOX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        VINOX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        VINOX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        VINOX_VERSION_STRING="${PROJECT_VERSION}"
)
target_include_directories(vinox_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    target_compile_options(vinox_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vinox_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(VINOX_OPENVINO_SDK_ROOT)
    find_package(
        OpenVINOGenAI CONFIG REQUIRED
        PATHS "${VINOX_OPENVINO_SDK_ROOT}/runtime/cmake"
        NO_DEFAULT_PATH
    )

    add_library(vinox_openvino SHARED
        src/backends/openvino/generator.cpp
    )
    add_library(vinox::openvino ALIAS vinox_openvino)
    set_target_properties(vinox_openvino PROPERTIES EXPORT_NAME openvino)
    target_compile_definitions(vinox_openvino PRIVATE VINOX_BUILD_SHARED)
    target_include_directories(vinox_openvino
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(vinox_openvino PRIVATE openvino::genai)

    if(MSVC)
        target_compile_options(vinox_openvino PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(vinox_openvino PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    install(
        TARGETS vinox_openvino
        EXPORT vinoxTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    if(WIN32)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_vinox_openvino_runtime_config Debug)
        else()
            set(_vinox_openvino_runtime_config Release)
        endif()

        install(
            DIRECTORY
                "${VINOX_OPENVINO_SDK_ROOT}/runtime/bin/intel64/${_vinox_openvino_runtime_config}/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            FILES_MATCHING
                PATTERN "*.dll"
                PATTERN "cache.json"
        )
        install(
            DIRECTORY "${VINOX_OPENVINO_SDK_ROOT}/runtime/3rdparty/tbb/bin/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            FILES_MATCHING
                PATTERN "*.dll"
                PATTERN "*_debug.dll" EXCLUDE
        )
        install(
            DIRECTORY "${VINOX_OPENVINO_SDK_ROOT}/docs/licensing/"
            DESTINATION licenses/openvino
        )
    endif()
endif()

add_executable(vinox_cli
    src/cli/main.cpp
)
target_link_libraries(vinox_cli PRIVATE vinox::core)
if(TARGET vinox_openvino)
    target_link_libraries(vinox_cli PRIVATE vinox_openvino)
endif()
set_target_properties(vinox_cli PROPERTIES OUTPUT_NAME "vinox-cli")
if(MSVC)
    target_compile_options(vinox_cli PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vinox_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(vinox_abi_smoke tests/abi_smoke.c)
    target_link_libraries(vinox_abi_smoke PRIVATE vinox::core)
    add_test(NAME abi_smoke COMMAND vinox_abi_smoke)

    if(TARGET vinox_openvino)
        add_executable(vinox_openvino_abi_smoke tests/openvino_abi_smoke.c)
        target_link_libraries(vinox_openvino_abi_smoke PRIVATE vinox::openvino)
        add_test(NAME openvino_abi_smoke COMMAND vinox_openvino_abi_smoke)
        if(WIN32 AND VINOX_OPENVINO_SDK_ROOT)
            set_tests_properties(openvino_abi_smoke PROPERTIES ENVIRONMENT
                "PATH=${VINOX_OPENVINO_SDK_ROOT}/runtime/bin/intel64/$<CONFIG>\\\;${VINOX_OPENVINO_SDK_ROOT}/runtime/3rdparty/tbb/bin\\\;$ENV{PATH}"
            )
        endif()
    endif()
endif()

install(
    TARGETS vinox_core vinox_cli
    EXPORT vinoxTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    EXPORT vinoxTargets
    NAMESPACE vinox::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vinox
)

configure_package_config_file(
    cmake/vinoxConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/vinoxConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vinox
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/vinoxConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/vinoxConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/vinoxConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vinox
)

configure_file(
    packaging/sbom.spdx.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/sbom.spdx.json
    @ONLY
)
install(FILES LICENSE NOTICE THIRD_PARTY_NOTICES.txt DESTINATION .)
install(FILES LICENSE NOTICE THIRD_PARTY_NOTICES.txt DESTINATION licenses)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sbom.spdx.json DESTINATION .)
install(DIRECTORY DESTINATION plugins/openvino)
install(DIRECTORY DESTINATION plugins/qt)
install(DIRECTORY DESTINATION plugins/vinox)
install(DIRECTORY DESTINATION qml)
install(DIRECTORY DESTINATION models)
install(DIRECTORY DESTINATION share/vinox/schemas)
install(DIRECTORY DESTINATION share/vinox/migrations)
install(DIRECTORY DESTINATION share/vinox/prompts)
install(DIRECTORY DESTINATION share/vinox/config)
