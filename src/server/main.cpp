#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "server_context.hpp"
#include "http_server.hpp"

namespace {

vinox::server::HttpServer* g_active_server{nullptr};

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n[VINOX-SERVER] Caught shutdown signal. Stopping server gracefully...\n";
        if (g_active_server) {
            g_active_server->stop();
        }
    }
}

void print_usage() {
    std::cout
        << "VINOX Server - OpenAI-compatible REST & SSE Server\n\n"
        << "Usage:\n"
        << "  vinox-server [options]\n\n"
        << "Options:\n"
        << "  --host <ip>          Host interface to bind (default: 127.0.0.1)\n"
        << "  --port <port>        Port to listen on (default: 8080)\n"
        << "  --model <path>       Path to OpenVINO model directory to preload\n"
        << "  --models-dir <dir>   Path to models root directory\n"
        << "  --device <dev>       Target device: CPU, GPU, NPU (default: CPU)\n"
        << "  --db <path>          Path to SQLite database (default: vinox.db)\n"
        << "  --embedding-model <p>Path to decoupled embedding model directory\n"
        << "  --embedding-device <d>Target device for embeddings: CPU, GPU, NPU (default: CPU)\n"
        << "  --api-key <key>      Require Bearer token authentication\n"
        << "  --cors               Enable CORS with permissive headers\n"
        << "  --help, -h           Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    vinox::server::ServerContext ctx;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--host" && i + 1 < argc) {
            ctx.host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            ctx.port = std::atoi(argv[++i]);
        } else if (arg == "--model" && i + 1 < argc) {
            ctx.model_path = argv[++i];
        } else if (arg == "--models-dir" && i + 1 < argc) {
            ctx.models_dir = argv[++i];
        } else if (arg == "--device" && i + 1 < argc) {
            ctx.device = argv[++i];
        } else if (arg == "--embedding-model" && i + 1 < argc) {
            ctx.embedding_model_path = argv[++i];
        } else if (arg == "--embedding-device" && i + 1 < argc) {
            ctx.embedding_device = argv[++i];
        } else if (arg == "--db" && i + 1 < argc) {
            ctx.db_path = argv[++i];
        } else if (arg == "--api-key" && i + 1 < argc) {
            ctx.api_key = argv[++i];
        } else if (arg == "--plugins-dir" && i + 1 < argc) {
            ctx.plugins_dir = argv[++i];
        } else if (arg == "--cors") {
            ctx.cors_enabled = true;
        }
    }

    std::cout << "================================================================================\n";
    std::cout << "  VINOX Server v0.1.0 — OpenAI Compatible HTTP/SSE Endpoint                    \n";
    std::cout << "================================================================================\n";

    // 0. Detect hardware devices with strict NPU priority
    detect_hardware_devices(ctx);
    std::cout << "[VINOX-SERVER] OpenVINO Execution Devices Detected (" << ctx.detected_devices.size() << "):\n";
    for (const auto& dev : ctx.detected_devices) {
        std::cout << "  - [" << (dev.priority == 1 ? "PRIORITY 1" : (dev.priority == 2 ? "PRIORITY 2" : "PRIORITY 3")) << "] "
                  << dev.device_id << ": " << dev.full_name << "\n";
    }
    std::cout << "[VINOX-SERVER] Primary Target Device: " << ctx.device 
              << (ctx.has_npu ? " (NPU Prioritized 🚀)" : "") << "\n";

    // 1. Initialize storage engine
    std::cout << "[VINOX-SERVER] Opening storage engine: " << ctx.db_path << " ... ";
    vinox_status st_st = vinox_storage_engine_open(ctx.db_path.c_str(), &ctx.storage);
    if (st_st == VINOX_STATUS_OK) {
        std::cout << "OK\n";
    } else {
        std::cerr << "WARNING: Storage engine failed to open: " << ctx.db_path << "\n";
    }

    // 2. Initialize model registry if configured or auto-detect
    if (ctx.models_dir.empty()) {
        if (!ctx.model_path.empty()) {
            namespace fs = std::filesystem;
            fs::path mp(ctx.model_path);
            if (fs::exists(mp.parent_path())) {
                ctx.models_dir = mp.parent_path().string();
            }
        }
        if (ctx.models_dir.empty() && std::filesystem::exists("C:/ai/models/OpenVINO")) {
            ctx.models_dir = "C:/ai/models/OpenVINO";
        }
    }

    if (!ctx.models_dir.empty()) {
        std::cout << "[VINOX-SERVER] Initializing model registry: " << ctx.models_dir << " ... ";
        if (vinox_model_registry_create(&ctx.registry) == VINOX_STATUS_OK) {
            size_t scanned = 0;
            vinox_model_registry_scan(ctx.registry, ctx.models_dir.c_str(), &scanned);
            std::cout << "Scanned " << scanned << " models\n";
        }
    }

    // 3. Preload model if specified
    if (!ctx.model_path.empty()) {
        inspect_model_directory(ctx.model_path, ctx);
        if (!ctx.model_badge_info.empty()) {
            std::cout << "[VINOX-SERVER] Model: " << ctx.model_badge_info << "\n";
            std::cout << "[VINOX-SERVER] Configuration: " << (ctx.has_custom_config ? "VINOX Config (vinox_config.json)" : "Hersteller-Defaults") << "\n";
        }
        std::cout << "[VINOX-SERVER] Preloading model: " << ctx.model_path << " on " << ctx.device << " ... ";
        vinox_model_options m_opts{};
        m_opts.struct_size = sizeof(m_opts);
        m_opts.model_path = ctx.model_path.c_str();
        m_opts.device = ctx.device.c_str();
        vinox_status m_st = vinox_model_load(&m_opts, &ctx.model);
        if (m_st != VINOX_STATUS_OK && ctx.device == "NPU") {
            std::cout << "NPU load unsupported for this model architecture, falling back to GPU... ";
            m_opts.device = "GPU";
            m_st = vinox_model_load(&m_opts, &ctx.model);
            if (m_st != VINOX_STATUS_OK) {
                std::cout << "GPU fallback failed, falling back to CPU... ";
                m_opts.device = "CPU";
                m_st = vinox_model_load(&m_opts, &ctx.model);
            }
            if (m_st == VINOX_STATUS_OK) {
                ctx.device = m_opts.device;
            }
        }
        if (m_st == VINOX_STATUS_OK) {
            std::cout << "OK (" << ctx.device << ")\n";

            // Attempt compiling template protocol if chat_template.jinja exists
            std::string tpl_path = ctx.model_path + "/chat_template.jinja";
            std::string tok_cfg_path = ctx.model_path + "/tokenizer_config.json";
            std::ifstream tpl_file(tpl_path);
            std::ifstream tok_file(tok_cfg_path);
            if (tpl_file.is_open()) {
                std::string tpl_str((std::istreambuf_iterator<char>(tpl_file)), std::istreambuf_iterator<char>());
                std::string tok_str;
                if (tok_file.is_open()) {
                    tok_str.assign((std::istreambuf_iterator<char>(tok_file)), std::istreambuf_iterator<char>());
                }
                ctx.protocol = vinox_model_protocol_contract{};
                ctx.protocol.struct_size = sizeof(ctx.protocol);
                if (vinox_model_protocol_compile(tpl_str.c_str(), tok_str.empty() ? nullptr : tok_str.c_str(), &ctx.protocol) == VINOX_STATUS_OK) {
                    ctx.has_protocol = true;
                    std::cout << "[VINOX-SERVER] Compiled model protocol: " << ctx.protocol.protocol_id << "\n";
                }
            }
        } else {
            std::cerr << "FAILED: " << vinox_openvino_last_error() << "\n";
        }
    }

    // 4. Preload decoupled embedding engine if specified or auto-detected
    if (ctx.embedding_model_path.empty() && !ctx.models_dir.empty()) {
        std::filesystem::path p1 = std::filesystem::path(ctx.models_dir) / "Qwen3-Embedding-0.6B-fp16-ov";
        std::filesystem::path p2 = std::filesystem::path(ctx.models_dir) / "Qwen3-Embedding-0.6B";
        if (std::filesystem::exists(p1 / "openvino_model.xml")) {
            ctx.embedding_model_path = p1.string();
            ctx.embedding_device = "CPU";
        } else if (std::filesystem::exists(p2 / "openvino_model.xml")) {
            ctx.embedding_model_path = p2.string();
            ctx.embedding_device = "CPU";
        }
    }

    if (!ctx.embedding_model_path.empty() && ctx.embedding_model_path != "none") {
        std::cout << "[VINOX-SERVER] Preloading decoupled embedding model: " << ctx.embedding_model_path << " on " << ctx.embedding_device << " ... ";
        vinox_embedding_options emb_opts{};
        emb_opts.struct_size = sizeof(emb_opts);
        emb_opts.model_path = ctx.embedding_model_path.c_str();
        emb_opts.device = ctx.embedding_device.c_str();
        emb_opts.pooling_mode = VINOX_EMBEDDING_POOLING_AUTO;
        emb_opts.normalization = VINOX_EMBEDDING_NORM_AUTO;
        emb_opts.enable_mmap = 1;
        emb_opts.enable_cache = 1;

        vinox_status emb_st = vinox_embedding_engine_create(&emb_opts, &ctx.embedding_engine);
        if (emb_st == VINOX_STATUS_OK) {
            size_t dim = 0;
            vinox_embedding_get_dim(ctx.embedding_engine, &dim);
            std::cout << "OK (Dim: " << dim << ")\n";
        } else {
            std::cerr << "FAILED: " << vinox_embedding_last_error() << "\n";
        }
    }

    // 5. Initialize tool registry & discover/load plugins
    vinox_tool_registry_create(&ctx.tool_registry);
    if (ctx.tool_registry) {
        void* init_ctx[2] = { static_cast<void*>(ctx.storage), static_cast<void*>(ctx.embedding_engine) };
        bool loaded_dynamic = false;

        // Check if plugins_dir exists (or adjacent to exe)
        std::filesystem::path pdir(ctx.plugins_dir);
        if (!std::filesystem::exists(pdir)) {
            auto candidate = std::filesystem::current_path() / ctx.plugins_dir;
            if (std::filesystem::exists(candidate)) pdir = candidate;
        }

        if (std::filesystem::exists(pdir) && std::filesystem::is_directory(pdir)) {
            std::cout << "[VINOX-SERVER] Scanning dynamic plugins directory: " << pdir.string() << "\n";
            for (const auto& entry : std::filesystem::directory_iterator(pdir)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    if (ext == ".dll" || ext == ".so" || ext == ".dylib") {
                        vinox_tool_plugin* dplug = nullptr;
                        vinox_status st = vinox_plugin_load_dynamic(entry.path().string().c_str(), nullptr, &dplug);
                        if (st == VINOX_STATUS_OK && dplug) {
                            if (dplug->init) {
                                dplug->init(dplug, init_ctx);
                            }
                            vinox_plugin_register(ctx.tool_registry, dplug);
                            ctx.loaded_plugins.push_back(dplug);
                            std::cout << "  [PLUGIN LOADED] " << (dplug->name ? dplug->name : entry.path().filename().string().c_str())
                                      << " v" << (dplug->version ? dplug->version : "1.0.0")
                                      << " (" << entry.path().filename().string() << ")\n";
                            loaded_dynamic = true;
                        } else {
                            std::cerr << "  [PLUGIN ERROR] Could not load " << entry.path().filename().string()
                                      << ": " << vinox_plugins_last_error() << "\n";
                        }
                    }
                }
            }
        }

        // Fallback: If no dynamic plugins were loaded, load built-ins
        if (!loaded_dynamic) {
            std::cout << "[VINOX-SERVER] Registering standard built-in plugins (std_fs, std_math, std_time, std_retrieval)\n";
            vinox_tool_plugin* plug_fs = nullptr;
            if (vinox_plugin_std_fs_create(".", &plug_fs) == VINOX_STATUS_OK) {
                vinox_plugin_register(ctx.tool_registry, plug_fs);
                ctx.loaded_plugins.push_back(plug_fs);
            }
            vinox_tool_plugin* plug_math = nullptr;
            if (vinox_plugin_std_math_create(&plug_math) == VINOX_STATUS_OK) {
                vinox_plugin_register(ctx.tool_registry, plug_math);
                ctx.loaded_plugins.push_back(plug_math);
            }
            vinox_tool_plugin* plug_time = nullptr;
            if (vinox_plugin_std_time_create(&plug_time) == VINOX_STATUS_OK) {
                vinox_plugin_register(ctx.tool_registry, plug_time);
                ctx.loaded_plugins.push_back(plug_time);
            }
            if (ctx.storage) {
                vinox_tool_plugin* plug_retrieval = nullptr;
                if (vinox_plugin_std_retrieval_create(ctx.storage, ctx.embedding_engine, &plug_retrieval) == VINOX_STATUS_OK) {
                    vinox_plugin_register(ctx.tool_registry, plug_retrieval);
                    ctx.loaded_plugins.push_back(plug_retrieval);
                }
            }
        }
    }

    ctx.mode_controller = vinox_mode_controller_create();

    // 6. Setup signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 7. Launch HTTP Server
    vinox::server::HttpServer server(ctx);
    g_active_server = &server;

    bool run_ok = server.start();

    // Cleanup
    std::cout << "[VINOX-SERVER] Cleaning up runtime resources...\n";
    if (ctx.model) {
        vinox_model_destroy(ctx.model);
        ctx.model = nullptr;
    }
    if (ctx.embedding_engine) {
        vinox_embedding_engine_destroy(ctx.embedding_engine);
        ctx.embedding_engine = nullptr;
    }
    if (ctx.storage) {
        vinox_storage_engine_close(ctx.storage);
        ctx.storage = nullptr;
    }
    if (ctx.registry) {
        vinox_model_registry_destroy(ctx.registry);
        ctx.registry = nullptr;
    }
    if (ctx.tool_registry) {
        for (auto* p : ctx.loaded_plugins) {
            vinox_plugin_destroy(p);
        }
        ctx.loaded_plugins.clear();
        vinox_tool_registry_destroy(ctx.tool_registry);
        ctx.tool_registry = nullptr;
    }
    if (ctx.mode_controller) {
        vinox_mode_controller_destroy(ctx.mode_controller);
        ctx.mode_controller = nullptr;
    }

    std::cout << "[VINOX-SERVER] Server stopped.\n";
    return run_ok ? 0 : 1;
}
