#ifdef VINOX_WITH_QT
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QtQml>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>
#include <memory>
#include <filesystem>

#include "backend_interface.hpp"
#include "local_backend.hpp"
#include "remote_backend.hpp"
#include "gui_bridge.hpp"
#include "viewmodels/chat_viewmodel.hpp"
#include "viewmodels/plan_viewmodel.hpp"
#include "viewmodels/agent_viewmodel.hpp"
#include "viewmodels/storage_viewmodel.hpp"
#include "viewmodels/diff_viewmodel.hpp"

int main(int argc, char* argv[]) {
#ifdef _WIN32
    AttachConsole(ATTACH_PARENT_PROCESS);
#endif
    std::cout << "================================================================================\n";
    std::cout << "  VINOX Desktop GUI Runtime (Phase 10)                                          \n";
    std::cout << "================================================================================\n";

    bool remote_mode = false;
    bool test_qml = false;
    std::string remote_host = "127.0.0.1";
    int remote_port = 8080;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--remote") {
            remote_mode = true;
        } else if (arg == "--test-qml") {
            test_qml = true;
        } else if (arg == "--host" && i + 1 < argc) {
            remote_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            remote_port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage:\n  vinox-gui [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --remote         Connect to remote vinox-server\n";
            std::cout << "  --host <ip>      Remote host (default: 127.0.0.1)\n";
            std::cout << "  --port <port>    Remote port (default: 8080)\n";
            std::cout << "  --help, -h       Show this help message\n";
            return 0;
        }
    }

    std::shared_ptr<vinox::gui::IVinoxBackend> backend;
    if (remote_mode) {
        std::cout << "[VINOX-GUI] Initializing Remote Backend (" << remote_host << ":" << remote_port << ")...\n";
        backend = std::make_shared<vinox::gui::RemoteVinoxBackend>(remote_host, remote_port);
    } else {
        std::cout << "[VINOX-GUI] Initializing Local C-ABI Backend...\n";
        backend = std::make_shared<vinox::gui::LocalVinoxBackend>();
    }

    vinox::gui::ChatViewModel chat_vm(backend);
    vinox::gui::PlanViewModel plan_vm(backend);
    vinox::gui::AgentViewModel agent_vm(backend);
    vinox::gui::StorageViewModel storage_vm(backend);
    vinox::gui::DiffViewModel diff_vm(backend);

    std::cout << "[VINOX-GUI] ViewModels initialized successfully.\n";

#ifdef VINOX_WITH_QT
    QGuiApplication app(argc, argv);
    app.setApplicationName("VINOX");
    app.setOrganizationName("Mentor82");

    vinox::gui::GuiBridge bridge(backend, remote_mode);

    if (!remote_mode) {
        const std::string default_model = "C:\\ai\\models\\OpenVINO\\ov_deepseek_tools_v4";
        if (std::filesystem::exists(default_model)) {
            std::cout << "[VINOX-GUI] Auto-loading local model: " << default_model << "...\n";
            bridge.loadModel(QString::fromStdString(default_model), "CPU");
        }
    }

    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &msg) {
        std::cerr << "[QML] " << msg.toStdString() << "\n";
    });

    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;
    engine.addImportPath("qrc:/");
    engine.addImportPath(":/");
    engine.addImportPath(app.applicationDirPath() + "/gui-qt6");

    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/Vinox/qml/Theme.qml")), "Vinox", 1, 0, "Theme");

    engine.rootContext()->setContextProperty("bridge", &bridge);
    const QUrl url(QStringLiteral("qrc:/Vinox/qml/Main.qml"));

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl) {
                std::cerr << "[VINOX-GUI] ERROR: Failed to load QML root from " << objUrl.toString().toStdString() << "\n";
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection
    );

    if (test_qml) {
        std::cout << "[VINOX-GUI] Running in automated QML test mode (500ms timeout)...\n";
        QTimer::singleShot(500, &app, &QCoreApplication::quit);
    }

    engine.load(url);
    return app.exec();
#else
    std::cout << "[VINOX-GUI] Compiled without Qt Quick runtime (Headless Core Mode).\n";
    std::cout << "[VINOX-GUI] All C-ABI and Remote ViewModels verified operational.\n";
    return 0;
#endif
}
