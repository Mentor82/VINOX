#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <set>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#endif

#include <thread>
#include <chrono>

namespace fs = std::filesystem;

// Path containment verifier preventing sandbox overlay root escape
static bool verify_path_containment(const fs::path& overlay_root, const fs::path& requested_file, fs::path& canonical_out) {
    try {
        fs::path canonical_root = fs::weakly_canonical(overlay_root);
        fs::path target_path = fs::weakly_canonical(overlay_root / requested_file);

        std::string root_str = canonical_root.string();
        std::string target_str = target_path.string();

        std::replace(root_str.begin(), root_str.end(), '\\', '/');
        std::replace(target_str.begin(), target_str.end(), '\\', '/');

        if (root_str.back() != '/') root_str.push_back('/');

        if (target_str.rfind(root_str, 0) != 0 && target_str != root_str.substr(0, root_str.length() - 1)) {
            return false; // Path containment violation / sandbox escape attempt!
        }

        canonical_out = target_path;
        return true;
    } catch (...) {
        return false;
    }
}

int main(int argc, char* argv[]) {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    std::string overlay_dir = ".sandbox_overlay";
    if (argc > 1) overlay_dir = argv[1];

    fs::create_directories(overlay_dir);

    // Explicitly allowed sandbox tools
    static const std::set<std::string> ALLOWED_SANDBOX_TOOLS = {
        "fs_write",
        "local_write.write",
        "fs_read",
        "local_read.read",
        "test_sleep"
    };

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        try {
            auto req = nlohmann::json::parse(line);
            nlohmann::json res;
            res["jsonrpc"] = "2.0";
            if (req.contains("id")) res["id"] = req["id"];

            std::string method = req.value("method", "");

            if (method == "sandbox/ping") {
                res["result"]["status"] = "pong";
                res["result"]["worker_pid"] = static_cast<int>(
#if defined(_WIN32)
                    GetCurrentProcessId()
#else
                    0
#endif
                );
            } else if (method == "sandbox/exec_tool") {
                std::string tool_name = req["params"].value("name", "");
                auto args = req["params"].value("arguments", nlohmann::json::object());

                // Nephy Finding 7: Unknown tools must fail closed with typed error (zero synthetic text fallback!)
                if (ALLOWED_SANDBOX_TOOLS.find(tool_name) == ALLOWED_SANDBOX_TOOLS.end()) {
                    res["error"]["code"] = -32601;
                    res["error"]["message"] = "Unknown sandbox tool: " + tool_name;
                } else if (tool_name == "fs_write" || tool_name == "local_write.write") {
                    if (!args.is_object() || !args.contains("filename") || !args["filename"].is_string()) {
                        res["error"]["code"] = -32602;
                        res["error"]["message"] = "Invalid or missing 'filename' argument";
                    } else {
                        std::string filename = args["filename"].get<std::string>();
                        std::string content = args.value("content", "");

                        fs::path canonical_file;
                        if (!verify_path_containment(fs::path(overlay_dir), fs::path(filename), canonical_file)) {
                            res["error"]["code"] = -32002;
                            res["error"]["message"] = "Path containment violation: attempt to escape sandbox overlay root";
                        } else {
                            fs::create_directories(canonical_file.parent_path());
                            std::ofstream out(canonical_file, std::ios::binary);
                            out << content;
                            out.close();

                            res["result"]["status"] = "OK";
                            res["result"]["bytes_written"] = content.length();
                            res["result"]["target_path"] = canonical_file.string();
                        }
                    }
                } else if (tool_name == "fs_read" || tool_name == "local_read.read") { // Real file read execution!
                    if (!args.is_object() || !args.contains("filename") || !args["filename"].is_string()) {
                        res["error"]["code"] = -32602;
                        res["error"]["message"] = "Invalid or missing 'filename' argument";
                    } else {
                        std::string filename = args["filename"].get<std::string>();
                        fs::path canonical_file;
                        if (!verify_path_containment(fs::path(overlay_dir), fs::path(filename), canonical_file)) {
                            res["error"]["code"] = -32002;
                            res["error"]["message"] = "Path containment violation: attempt to escape sandbox overlay root";
                        } else if (!fs::exists(canonical_file)) {
                            res["error"]["code"] = -32001;
                            res["error"]["message"] = "File not found in sandbox overlay";
                        } else {
                            std::ifstream in(canonical_file, std::ios::binary);
                            std::string file_content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                            res["result"]["status"] = "OK";
                            res["result"]["content"] = file_content;
                            res["result"]["bytes_read"] = file_content.length();
                        }
                    }
                } else if (tool_name == "test_sleep") {
                    if (args.contains("handshake_file") && args["handshake_file"].is_string()) {
                        std::string hfile = args["handshake_file"].get<std::string>();
                        fs::path canonical_hfile;
                        if (verify_path_containment(fs::path(overlay_dir), fs::path(hfile), canonical_hfile)) {
                            fs::create_directories(canonical_hfile.parent_path());
                            std::ofstream out(canonical_hfile, std::ios::binary);
                            out << "DISPATCH_STARTED\n";
                            out.close();
                        }
                    }
                    int delay_ms = args.value("delay_ms", 2000);
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                    res["result"]["status"] = "OK";
                    res["result"]["slept_ms"] = delay_ms;
                } else {
                    res["error"]["code"] = -32601;
                    res["error"]["message"] = "Unsupported sandbox tool: " + tool_name;
                }
            } else if (method == "sandbox/shutdown") {
                res["result"]["status"] = "shutdown_ack";
                std::string res_str = res.dump() + "\n";
                std::cout << res_str;
                std::cout.flush();
                break;
            } else {
                res["error"]["code"] = -32601;
                res["error"]["message"] = "Method not found";
            }

            std::string res_str = res.dump() + "\n";
            std::cout << res_str;
            std::cout.flush();
        } catch (...) {
            nlohmann::json err_res;
            err_res["jsonrpc"] = "2.0";
            err_res["error"]["code"] = -32700;
            err_res["error"]["message"] = "Parse error: malformed JSON request";
            std::cout << err_res.dump() << "\n";
            std::cout.flush();
        }
    }

    return 0;
}
