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
        src/backends/openvino/embedding.cpp
        src/backends/openvino/minja_template_runtime.cpp
    )
    add_library(vinox::openvino ALIAS vinox_openvino)
    set_target_properties(vinox_openvino PROPERTIES EXPORT_NAME openvino)
    target_compile_definitions(vinox_openvino PRIVATE VINOX_BUILD_SHARED)
    target_include_directories(vinox_openvino
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/thirdparty/minja
    )
    target_link_libraries(vinox_openvino PRIVATE openvino::genai nlohmann_json::nlohmann_json)

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

target_link_libraries(vinox_agent PUBLIC vinox::core PRIVATE nlohmann_json::nlohmann_json)
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

add_library(vinox_plugins SHARED
    src/core/plugins.cpp
)
add_library(vinox::plugins ALIAS vinox_plugins)
set_target_properties(vinox_plugins PROPERTIES EXPORT_NAME plugins)

target_link_libraries(vinox_plugins PRIVATE vinox::core vinox::storage nlohmann_json::nlohmann_json)
if(TARGET vinox_openvino)
    target_link_libraries(vinox_plugins PRIVATE vinox::openvino)
endif()
target_compile_definitions(vinox_plugins PRIVATE VINOX_BUILD_SHARED)
target_include_directories(vinox_plugins
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# Standalone Dynamic Tool Plugins
foreach(plug_name time math fs retrieval)
    add_library(vinox_plugin_${plug_name} SHARED
        src/plugins/${plug_name}/plugin_${plug_name}.cpp
    )
    target_link_libraries(vinox_plugin_${plug_name} PRIVATE vinox::plugins vinox::core)
    target_compile_definitions(vinox_plugin_${plug_name} PRIVATE VINOX_BUILD_SHARED)
    target_include_directories(vinox_plugin_${plug_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    if(MSVC)
        target_compile_options(vinox_plugin_${plug_name} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(vinox_plugin_${plug_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_custom_command(TARGET vinox_plugin_${plug_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:vinox_plugin_${plug_name}>/plugins
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:vinox_plugin_${plug_name}> $<TARGET_FILE_DIR:vinox_plugin_${plug_name}>/plugins/
    )
endforeach()

add_library(vinox_transport_linep SHARED
    src/transport/vinox_transport_linep.cpp
)
add_library(vinox::transport_linep ALIAS vinox_transport_linep)
set_target_properties(vinox_transport_linep PROPERTIES EXPORT_NAME transport_linep)

target_link_libraries(vinox_transport_linep PRIVATE vinox::core vinox::serving nlohmann_json::nlohmann_json)
if(TARGET vinox_openvino)
    target_link_libraries(vinox_transport_linep PRIVATE vinox::openvino)
endif()
target_compile_definitions(vinox_transport_linep PRIVATE VINOX_BUILD_SHARED)
target_include_directories(vinox_transport_linep
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(MSVC)
    target_compile_options(vinox_transport_linep PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vinox_transport_linep PRIVATE -Wall -Wextra -Wpedantic)
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
target_link_libraries(vinox_cli PRIVATE vinox::core vinox::serving vinox::storage vinox::agent vinox::plugins vinox::transport_linep nlohmann_json::nlohmann_json)
if(TARGET vinox_openvino)
    target_link_libraries(vinox_cli PRIVATE vinox::openvino)
endif()
set_target_properties(vinox_cli PROPERTIES OUTPUT_NAME "vinox-cli")
if(MSVC)
    target_compile_options(vinox_cli PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vinox_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(vinox_server
    src/server/main.cpp
    src/server/http_server.cpp
    src/server/openai_handlers.cpp
    src/server/agent_handlers.cpp
)
target_include_directories(vinox_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vinox_server PRIVATE vinox::core vinox::serving vinox::storage vinox::agent vinox::plugins nlohmann_json::nlohmann_json spdlog::spdlog)
if(TARGET vinox_openvino)
    target_link_libraries(vinox_server PRIVATE vinox::openvino)
endif()
if(WIN32)
    target_link_libraries(vinox_server PRIVATE ws2_32)
endif()
set_target_properties(vinox_server PROPERTIES OUTPUT_NAME "vinox-server")
if(MSVC)
    target_compile_options(vinox_server PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(vinox_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 COMPONENTS Core Gui Qml Quick QuickControls2 QUIET)

if(TARGET Qt6::Core AND TARGET Qt6::Quick)
    add_executable(vinox_gui
        src/gui/main.cpp
        src/gui/local_backend.cpp
        src/gui/remote_backend.cpp
        src/gui/gui_bridge.cpp
        src/gui/viewmodels/chat_viewmodel.cpp
        src/gui/viewmodels/plan_viewmodel.cpp
        src/gui/viewmodels/agent_viewmodel.cpp
        src/gui/viewmodels/storage_viewmodel.cpp
        src/gui/viewmodels/diff_viewmodel.cpp
    )
    target_include_directories(vinox_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(vinox_gui PRIVATE vinox::core vinox::serving vinox::storage vinox::agent nlohmann_json::nlohmann_json spdlog::spdlog)
    if(TARGET vinox_openvino)
        target_link_libraries(vinox_gui PRIVATE vinox::openvino)
    endif()
    if(WIN32)
        target_link_libraries(vinox_gui PRIVATE ws2_32 ole32 shell32)
    endif()
    target_compile_definitions(vinox_gui PRIVATE VINOX_WITH_QT=1)
    target_link_libraries(vinox_gui PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    set_target_properties(vinox_gui PROPERTIES OUTPUT_NAME "vinox-gui")
    if(MSVC)
        target_compile_options(vinox_gui PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(vinox_gui PRIVATE -Wall -Wextra -Wpedantic)
    endif()
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
    target_link_libraries(vinox_mcp_server_smoke PRIVATE vinox::core vinox::storage)
    add_dependencies(vinox_mcp_server_smoke vinox_mcp_server)
    add_test(NAME mcp_server_smoke COMMAND vinox_mcp_server_smoke)

    add_executable(vinox_agent_smoke tests/agent_smoke.c)
    target_link_libraries(vinox_agent_smoke PRIVATE vinox::agent)
    add_test(NAME agent_smoke COMMAND vinox_agent_smoke)

    add_executable(vinox_sandbox_smoke tests/sandbox_smoke.c)
    target_link_libraries(vinox_sandbox_smoke PRIVATE vinox::agent)
    add_dependencies(vinox_sandbox_smoke vinox_sandbox_worker)
    add_test(NAME sandbox_smoke COMMAND vinox_sandbox_smoke)

    add_executable(vinox_core_e2e_smoke tests/core_e2e_smoke.c)
    target_link_libraries(vinox_core_e2e_smoke PRIVATE vinox::agent vinox::storage vinox::core)
    add_dependencies(vinox_core_e2e_smoke vinox_sandbox_worker vinox_cli)
    add_test(NAME core_e2e_smoke COMMAND vinox_core_e2e_smoke)

    add_executable(vinox_cli_process_smoke tests/cli_process_smoke.cpp)
    target_include_directories(vinox_cli_process_smoke PRIVATE include)
    target_link_libraries(vinox_cli_process_smoke PRIVATE nlohmann_json::nlohmann_json vinox_agent vinox_core)
    add_dependencies(vinox_cli_process_smoke vinox_cli vinox_sandbox_worker)
    add_test(NAME cli_process_smoke COMMAND vinox_cli_process_smoke)

    add_executable(vinox_linep_transport_smoke tests/linep_transport_smoke.cpp)
    target_include_directories(vinox_linep_transport_smoke PRIVATE include)
    target_link_libraries(vinox_linep_transport_smoke PRIVATE vinox::transport_linep vinox::core)
    add_test(NAME linep_transport_smoke COMMAND vinox_linep_transport_smoke)

    add_executable(vinox_linep_outbound_lease_smoke tests/linep_outbound_lease_smoke.cpp)
    target_include_directories(vinox_linep_outbound_lease_smoke PRIVATE include)
    target_link_libraries(vinox_linep_outbound_lease_smoke PRIVATE vinox::transport_linep vinox::core)
    add_test(NAME linep_outbound_lease_smoke COMMAND vinox_linep_outbound_lease_smoke)

    add_executable(vinox_conformance_test_runner tests/conformance_test_runner.cpp)
    target_include_directories(vinox_conformance_test_runner PRIVATE include)
    target_link_libraries(vinox_conformance_test_runner PRIVATE vinox::transport_linep vinox::core)

    add_test(NAME audit_smoke COMMAND vinox_cli --audit)
    if(WIN32 AND VINOX_OPENVINO_SDK_ROOT)
        set_tests_properties(audit_smoke cli_process_smoke PROPERTIES ENVIRONMENT
            "PATH=${VINOX_OPENVINO_SDK_ROOT}/runtime/bin/intel64/$<CONFIG>\\\;${VINOX_OPENVINO_SDK_ROOT}/runtime/3rdparty/tbb/bin\\\;$ENV{PATH}"
        )
    endif()

    if(TARGET vinox_openvino)
        add_executable(vinox_openvino_abi_smoke tests/openvino_abi_smoke.c)
        target_link_libraries(vinox_openvino_abi_smoke PRIVATE vinox::openvino)
        add_test(NAME openvino_abi_smoke COMMAND vinox_openvino_abi_smoke)

        add_executable(vinox_reasoning_channel_smoke tests/reasoning_channel_smoke.cpp)
        target_link_libraries(vinox_reasoning_channel_smoke PRIVATE vinox::openvino vinox::core)
        add_test(NAME reasoning_channel_smoke COMMAND vinox_reasoning_channel_smoke)

        add_executable(vinox_model_protocol_compiler_smoke tests/model_protocol_compiler_smoke.cpp)
        target_link_libraries(vinox_model_protocol_compiler_smoke PRIVATE vinox::openvino vinox::core nlohmann_json::nlohmann_json)
        add_test(NAME model_protocol_compiler_smoke COMMAND vinox_model_protocol_compiler_smoke)

        add_executable(vinox_realtime_evidence_harness tests/realtime_evidence_harness.cpp)
        target_link_libraries(vinox_realtime_evidence_harness PRIVATE vinox::openvino vinox::core nlohmann_json::nlohmann_json)
        add_test(NAME realtime_evidence_harness COMMAND vinox_realtime_evidence_harness)

        add_executable(vinox_template_protocol_evidence_probe tests/template_protocol_evidence_probe.cpp)
        target_link_libraries(vinox_template_protocol_evidence_probe PRIVATE vinox::openvino vinox::core nlohmann_json::nlohmann_json)
        add_test(NAME template_protocol_evidence_probe COMMAND vinox_template_protocol_evidence_probe)

        # MinjaTemplateRuntime is an internal (non-ABI) C++ class, deliberately
        # not exported across the vinox_openvino DLL boundary (Architekturregeln,
        # PLAN.md #2: "STL-Typen und Exceptions ueberschreiten keine DLL-Grenze").
        # This smoke test therefore compiles the runtime's .cpp directly into the
        # test binary rather than linking against vinox::openvino.
        add_executable(vinox_minja_template_runtime_smoke
            tests/minja_template_runtime_smoke.cpp
            src/backends/openvino/minja_template_runtime.cpp
        )
        target_include_directories(vinox_minja_template_runtime_smoke PRIVATE src src/thirdparty/minja)
        target_link_libraries(vinox_minja_template_runtime_smoke PRIVATE nlohmann_json::nlohmann_json)
        add_test(NAME minja_template_runtime_smoke COMMAND vinox_minja_template_runtime_smoke)

        add_executable(vinox_nvme_cache_smoke tests/nvme_cache_smoke.cpp)
        target_link_libraries(vinox_nvme_cache_smoke PRIVATE vinox::openvino vinox::core)
        add_test(NAME nvme_cache_smoke COMMAND vinox_nvme_cache_smoke)

        add_executable(vinox_nvme_mmap_cache_benchmark tests/nvme_mmap_cache_benchmark.cpp)
        target_link_libraries(vinox_nvme_mmap_cache_benchmark PRIVATE vinox::openvino vinox::core psapi)

        add_executable(vinox_qwen2_5_eval_smoke tests/qwen2_5_eval_smoke.cpp)
        target_link_libraries(vinox_qwen2_5_eval_smoke PRIVATE vinox::core vinox::openvino vinox::serving vinox::storage nlohmann_json::nlohmann_json)
        file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests)
        add_test(NAME qwen2_5_eval_smoke COMMAND vinox_qwen2_5_eval_smoke)

        file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/schemas DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
        add_executable(vinox_server_smoke
            tests/server_smoke.cpp
            src/server/http_server.cpp
            src/server/openai_handlers.cpp
            src/server/agent_handlers.cpp
        )
        target_include_directories(vinox_server_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(vinox_server_smoke PRIVATE vinox::core vinox::serving vinox::storage vinox::agent vinox::plugins nlohmann_json::nlohmann_json spdlog::spdlog)
        if(TARGET vinox_openvino)
            target_link_libraries(vinox_server_smoke PRIVATE vinox::openvino)
        endif()
        if(WIN32)
            target_link_libraries(vinox_server_smoke PRIVATE ws2_32)
        endif()
        add_test(NAME server_smoke COMMAND vinox_server_smoke)
        add_test(NAME server_help_smoke COMMAND vinox_server --help)

        add_executable(vinox_gui_viewmodel_smoke
            tests/gui_viewmodel_smoke.cpp
            src/gui/local_backend.cpp
            src/gui/remote_backend.cpp
            src/gui/viewmodels/chat_viewmodel.cpp
            src/gui/viewmodels/plan_viewmodel.cpp
            src/gui/viewmodels/agent_viewmodel.cpp
            src/gui/viewmodels/storage_viewmodel.cpp
            src/gui/viewmodels/diff_viewmodel.cpp
        )
        target_include_directories(vinox_gui_viewmodel_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(vinox_gui_viewmodel_smoke PRIVATE vinox::core vinox::serving vinox::storage vinox::agent vinox::plugins nlohmann_json::nlohmann_json spdlog::spdlog)
        if(TARGET vinox_openvino)
            target_link_libraries(vinox_gui_viewmodel_smoke PRIVATE vinox::openvino)
        endif()
        if(WIN32)
            target_link_libraries(vinox_gui_viewmodel_smoke PRIVATE ws2_32)
        endif()
        add_test(NAME gui_viewmodel_smoke COMMAND vinox_gui_viewmodel_smoke)

        add_executable(vinox_plugins_smoke tests/plugins_smoke.cpp)
        target_link_libraries(vinox_plugins_smoke PRIVATE vinox::core vinox::storage vinox::plugins nlohmann_json::nlohmann_json)
        add_test(NAME plugins_smoke COMMAND vinox_plugins_smoke)

        add_executable(vinox_embedding_smoke tests/embedding_smoke.cpp)
        target_link_libraries(vinox_embedding_smoke PRIVATE vinox::core vinox::openvino)
        add_test(NAME embedding_smoke COMMAND vinox_embedding_smoke)

        if(WIN32 AND VINOX_OPENVINO_SDK_ROOT)
            set_tests_properties(openvino_abi_smoke reasoning_channel_smoke model_protocol_compiler_smoke qwen2_5_eval_smoke cli_process_smoke template_protocol_evidence_probe realtime_evidence_harness nvme_cache_smoke server_smoke server_help_smoke gui_viewmodel_smoke plugins_smoke embedding_smoke PROPERTIES ENVIRONMENT
                "PATH=${VINOX_OPENVINO_SDK_ROOT}/runtime/bin/intel64/Release\\\;${VINOX_OPENVINO_SDK_ROOT}/runtime/bin/intel64/Debug\\\;${VINOX_OPENVINO_SDK_ROOT}/runtime/3rdparty/tbb/bin\\\;${VINOX_OPENVINO_SDK_ROOT}/runtime/bin/intel64/$<CONFIG>\\\;$ENV{PATH}"
            )
            set_tests_properties(realtime_evidence_harness PROPERTIES TIMEOUT 2400)
        endif()
        set_tests_properties(qwen2_5_eval_smoke PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()

# ==============================================================================
# Standalone Distribution: Automatic Bundling of all Runtime Dependencies
# ==============================================================================
if(WIN32)
    # If Qt6 MinGW is installed on Windows, automatically build the Qt Quick GUI and deploy Qt runtime
    if(EXISTS "C:/Qt/6.10.1/mingw_64/bin/qmake.exe")
        add_custom_target(vinox_qt_gui_build ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/gui-qt6"
            COMMAND ${CMAKE_COMMAND} -G Ninja -S "${CMAKE_SOURCE_DIR}/src/gui" -B "${CMAKE_BINARY_DIR}/gui-qt6" -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe" -DCMAKE_C_COMPILER="C:/Qt/Tools/mingw1310_64/bin/gcc.exe" -DCMAKE_PREFIX_PATH="C:/Qt/6.10.1/mingw_64"
            COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}/gui-qt6"
            COMMAND "C:/Qt/6.10.1/mingw_64/bin/windeployqt.exe" --qmldir "${CMAKE_SOURCE_DIR}/src/gui/qml" "${CMAKE_BINARY_DIR}/gui-qt6/vinox-gui.exe"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/gui-qt6/vinox-gui.exe" "${CMAKE_CURRENT_BINARY_DIR}/vinox-gui.exe"
            COMMENT "Building native Qt Quick Desktop GUI and deploying Qt runtime"
        )
        add_dependencies(vinox_qt_gui_build vinox_core vinox_serving vinox_storage vinox_agent vinox_openvino)
    endif()

    add_custom_target(vinox_bundle_runtime_dlls ALL
        COMMAND ${CMAKE_COMMAND} -DDEST="${CMAKE_CURRENT_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle_runtime_dlls.cmake"
        COMMENT "Bundling all standalone runtime DLLs into build directory"
    )
    add_dependencies(vinox_cli vinox_bundle_runtime_dlls)
    add_dependencies(vinox_server vinox_bundle_runtime_dlls)
    if(TARGET vinox_gui)
        add_dependencies(vinox_gui vinox_bundle_runtime_dlls)
    endif()
    if(TARGET vinox_qt_gui_build)
        add_dependencies(vinox_bundle_runtime_dlls vinox_qt_gui_build)
    endif()

    install(CODE "
        file(TO_CMAKE_PATH \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\" _dest)
        string(REPLACE \"\\\"\" \"\" _dest \"\${_dest}\")
        execute_process(COMMAND \"${CMAKE_COMMAND}\" -DDEST=\${_dest} -P \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundle_runtime_dlls.cmake\")
    ")
endif()

set(VINOX_INSTALL_TARGETS vinox_core vinox_serving vinox_storage vinox_agent vinox_plugins vinox_sandbox_worker vinox_mcp_server vinox_cli vinox_server)
if(TARGET vinox_gui)
    list(APPEND VINOX_INSTALL_TARGETS vinox_gui)
endif()

install(
    TARGETS ${VINOX_INSTALL_TARGETS}
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

