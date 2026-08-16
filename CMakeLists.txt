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

find_package(nlohmann_json REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(vinox_core SHARED
    src/core/version.cpp
    src/core/logging.cpp
    src/core/tools.cpp
    src/core/mcp_client.cpp
)
add_library(vinox::core ALIAS vinox_core)
set_target_properties(vinox_core PROPERTIES EXPORT_NAME core)

target_link_libraries(vinox_core PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)
if(WIN32)
    target_link_libraries(vinox_core PRIVATE winhttp)
endif()

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
        install(
            DIRECTORY "${VINOX_OPENVINO_SDK_ROOT}/runtime/bin/intel64/Debug/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            CONFIGURATIONS Debug
            FILES_MATCHING
                PATTERN "*.dll"
                PATTERN "cache.json"
        )
        install(
            DIRECTORY "${VINOX_OPENVINO_SDK_ROOT}/runtime/bin/intel64/Release/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            CONFIGURATIONS Release RelWithDebInfo MinSizeRel
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

find_package(nlohmann_json REQUIRED)

add_library(vinox_serving SHARED
    src/serving/model_registry.cpp
    src/serving/context_budget.cpp
)
add_library(vinox::serving ALIAS vinox_serving)
set_target_properties(vinox_serving PROPERTIES EXPORT_NAME serving)

target_link_libraries(vinox_serving PRIVATE nlohmann_json::nlohmann_json vinox::core)
target_compile_definitions(vinox_serving PRIVATE VINOX_BUILD_SHARED)
target_include_directories(vinox_serving
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(MSVC)
    target_compile_options(vinox_serving PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vinox_serving PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(unofficial-sqlite3 REQUIRED)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/schemas/database/001_init.sql" CANONICAL_MIGRATION_001_CONTENT)
string(CONFIGURE [[
#ifndef VINOX_CANONICAL_MIGRATION_001_H
#define VINOX_CANONICAL_MIGRATION_001_H

namespace vinox::storage {
inline constexpr const char* CANONICAL_MIGRATION_001_SQL = R"(
@CANONICAL_MIGRATION_001_CONTENT@
)";
}

#endif
]] GENERATED_001_INIT_HEADER @ONLY)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/001_init_sql.h" "${GENERATED_001_INIT_HEADER}")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/schemas/database/002_documents_relations.sql" CANONICAL_MIGRATION_002_CONTENT)
string(CONFIGURE [[
#ifndef VINOX_CANONICAL_MIGRATION_002_H
#define VINOX_CANONICAL_MIGRATION_002_H

namespace vinox::storage {
inline constexpr const char* CANONICAL_MIGRATION_002_SQL = R"(
@CANONICAL_MIGRATION_002_CONTENT@
)";
}

#endif
]] GENERATED_002_DOCS_REL_HEADER @ONLY)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/002_documents_relations_sql.h" "${GENERATED_002_DOCS_REL_HEADER}")

add_library(vinox_storage SHARED
    src/storage/db.cpp
    src/storage/embeddings.cpp
    src/storage/vector_index.cpp
    src/storage/sqlite-vec/sqlite-vec.c
)
add_library(vinox::storage ALIAS vinox_storage)
set_target_properties(vinox_storage PROPERTIES EXPORT_NAME storage)

target_link_libraries(vinox_storage PRIVATE unofficial::sqlite3::sqlite3 vinox::core)
target_compile_definitions(vinox_storage PRIVATE VINOX_BUILD_SHARED SQLITE_VEC_STATIC SQLITE_CORE)
target_include_directories(vinox_storage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/storage
)
if(MSVC)
    target_compile_options(vinox_storage PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vinox_storage PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(vinox_agent SHARED
    src/agent/mode_controller.cpp
    src/agent/plan_model.cpp
    src/agent/agent_engine.cpp
    src/agent/artifact_commit.cpp
    src/sandbox/sandbox_host.cpp
)
add_library(vinox::agent ALIAS vinox_agent)
set_target_properties(vinox_agent PROPERTIES EXPORT_NAME agent)

target_link_libraries(vinox_agent PRIVATE vinox::core nlohmann_json::nlohmann_json)
target_compile_definitions(vinox_agent PRIVATE VINOX_BUILD_SHARED)
target_include_directories(vinox_agent
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/agent
)
if(MSVC)
    target_compile_options(vinox_agent PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vinox_agent PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(vinox_sandbox_worker
    src/sandbox/worker_main.cpp
)
target_link_libraries(vinox_sandbox_worker PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(vinox_sandbox_worker PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vinox_sandbox_worker PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(vinox_mcp_server
    src/mcp/server_main.cpp
)
target_link_libraries(vinox_mcp_server PRIVATE vinox::core vinox::storage nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(vinox_mcp_server PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vinox_mcp_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(vinox_cli
    src/cli/main.cpp
)
target_link_libraries(vinox_cli PRIVATE vinox::core vinox::serving vinox::storage vinox::agent)
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

    add_executable(vinox_provenance_smoke tests/provenance_smoke.c)
    target_link_libraries(vinox_provenance_smoke PRIVATE vinox::core)
    add_test(NAME provenance_smoke COMMAND vinox_provenance_smoke)

    add_executable(vinox_logging_smoke tests/logging_smoke.c)
    target_link_libraries(vinox_logging_smoke PRIVATE vinox::core vinox::storage)
    add_test(NAME logging_smoke COMMAND vinox_logging_smoke)

    add_executable(vinox_serving_smoke tests/serving_smoke.c)
    target_link_libraries(vinox_serving_smoke PRIVATE vinox::serving)
    add_test(NAME serving_smoke COMMAND vinox_serving_smoke)

    add_executable(vinox_storage_smoke tests/storage_smoke.c)
    target_link_libraries(vinox_storage_smoke PRIVATE vinox::storage unofficial::sqlite3::sqlite3)
    add_test(NAME storage_smoke COMMAND vinox_storage_smoke)

    add_executable(vinox_tools_smoke tests/tools_smoke.c)
    target_link_libraries(vinox_tools_smoke PRIVATE vinox::core)
    add_test(NAME tools_smoke COMMAND vinox_tools_smoke)

    add_executable(vinox_mcp_fixture_server tests/mcp_fixture_server.cpp)
    target_link_libraries(vinox_mcp_fixture_server PRIVATE nlohmann_json::nlohmann_json)

    add_executable(vinox_mcp_http_fixture_server tests/mcp_http_fixture_server.cpp)
    target_link_libraries(vinox_mcp_http_fixture_server PRIVATE nlohmann_json::nlohmann_json)
    if(WIN32)
        target_link_libraries(vinox_mcp_http_fixture_server PRIVATE ws2_32)
    endif()

    add_executable(vinox_mcp_smoke tests/mcp_smoke.c)
    target_link_libraries(vinox_mcp_smoke PRIVATE vinox::core)
    add_dependencies(vinox_mcp_smoke vinox_mcp_fixture_server vinox_mcp_http_fixture_server)
    add_test(NAME mcp_smoke COMMAND vinox_mcp_smoke)

    add_executable(vinox_docker_mcp_live_test tests/docker_mcp_live_test.cpp)
    target_link_libraries(vinox_docker_mcp_live_test PRIVATE vinox::core nlohmann_json::nlohmann_json)

    add_executable(vinox_mcp_server_smoke tests/mcp_server_smoke.c)
    target_link_libraries(vinox_mcp_server_smoke PRIVATE vinox::core)
    add_dependencies(vinox_mcp_server_smoke vinox_mcp_server)
    add_test(NAME mcp_server_smoke COMMAND vinox_mcp_server_smoke)

    add_executable(vinox_agent_smoke tests/agent_smoke.c)
    target_link_libraries(vinox_agent_smoke PRIVATE vinox::agent)
    add_test(NAME agent_smoke COMMAND vinox_agent_smoke)

    add_executable(vinox_sandbox_smoke tests/sandbox_smoke.c)
    target_link_libraries(vinox_sandbox_smoke PRIVATE vinox::agent)
    add_dependencies(vinox_sandbox_smoke vinox_sandbox_worker)
    add_test(NAME sandbox_smoke COMMAND vinox_sandbox_smoke)

    add_test(NAME audit_smoke COMMAND vinox_cli --audit)
    if(WIN32 AND VINOX_OPENVINO_SDK_ROOT)
        set_tests_properties(audit_smoke PROPERTIES ENVIRONMENT
            "PATH=${VINOX_OPENVINO_SDK_ROOT}/runtime/bin/intel64/$<CONFIG>\\\;${VINOX_OPENVINO_SDK_ROOT}/runtime/3rdparty/tbb/bin\\\;$ENV{PATH}"
        )
    endif()

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
    TARGETS vinox_core vinox_serving vinox_storage vinox_cli
    EXPORT vinoxTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY schemas/ DESTINATION share/vinox/schemas)
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

