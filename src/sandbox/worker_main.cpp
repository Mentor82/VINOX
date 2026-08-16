#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#endif

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    std::string overlay_dir = ".sandbox_overlay";
    if (argc > 1) overlay_dir = argv[1];

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

                if (tool_name == "fs_write" || tool_name == "local_write.write") {
                    std::string filename = args.value("filename", "output.txt");
                    std::string content = args.value("content", "");

                    fs::path file_path = fs::path(overlay_dir) / filename;
                    fs::create_directories(file_path.parent_path());

                    std::ofstream out(file_path, std::ios::binary);
                    out << content;
                    out.close();

                    res["result"]["status"] = "OK";
                    res["result"]["bytes_written"] = content.length();
                    res["result"]["target_path"] = file_path.string();
                } else {
                    res["result"]["status"] = "OK";
                    res["result"]["output"] = "Tool executed successfully in sandbox worker";
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
            err_res["error"]["message"] = "Parse error";
            std::cout << err_res.dump() << "\n";
            std::cout.flush();
        }
    }

    return 0;
}
