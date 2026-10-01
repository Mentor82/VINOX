cmake_minimum_required(VERSION 3.20)
project(vinox_gui_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2)
qt_standard_project_setup()

# Use vendored httplib and nlohmann_json
set(VINOX_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

qt_add_executable(vinox-gui
    main.cpp
    backend_interface.hpp
    remote_backend.hpp
    remote_backend.cpp
    local_backend.hpp
    local_backend.cpp
    gui_bridge.hpp
    gui_bridge.cpp
    viewmodels/chat_viewmodel.hpp
    viewmodels/chat_viewmodel.cpp
    viewmodels/plan_viewmodel.hpp
    viewmodels/plan_viewmodel.cpp
    viewmodels/agent_viewmodel.hpp
    viewmodels/agent_viewmodel.cpp
    viewmodels/storage_viewmodel.hpp
    viewmodels/storage_viewmodel.cpp
    viewmodels/diff_viewmodel.hpp
    viewmodels/diff_viewmodel.cpp
)

target_compile_definitions(vinox-gui PRIVATE VINOX_WITH_QT)
target_include_directories(vinox-gui PRIVATE
    ${VINOX_SRC_ROOT}
    ${VINOX_SRC_ROOT}/include
    ${VINOX_SRC_ROOT}/out/windows-msvc-debug/vcpkg_installed/x64-windows/include
)

target_link_libraries(vinox-gui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    ws2_32
    ole32
    shell32
    ${VINOX_SRC_ROOT}/out/windows-msvc-debug/build/vinox_core.lib
    ${VINOX_SRC_ROOT}/out/windows-msvc-debug/build/vinox_serving.lib
    ${VINOX_SRC_ROOT}/out/windows-msvc-debug/build/vinox_storage.lib
    ${VINOX_SRC_ROOT}/out/windows-msvc-debug/build/vinox_agent.lib
    ${VINOX_SRC_ROOT}/out/windows-msvc-debug/build/vinox_openvino.lib
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(vinox-gui
    URI Vinox
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/components/TelemetryPill.qml
        qml/components/CapabilityStatusWidget.qml
        qml/components/ReasoningCard.qml
        qml/components/MessageCard.qml
        qml/components/RetrievalResultCard.qml
        qml/components/NavigationItem.qml
        qml/views/ChatView.qml
        qml/views/PlanView.qml
        qml/views/AgentView.qml
        qml/views/DiffView.qml
        qml/views/SearchView.qml
        qml/views/RelationsView.qml
        qml/views/McpBrowserView.qml
        qml/views/SettingsView.qml
)
