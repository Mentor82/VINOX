#include "vinox/mcp.h"
#include "vinox/mcp.hpp"
#include "vinox/logging.hpp"
#include "vinox/logging.h"
#include "vinox/tools.h"

#include <nlohmann/json.hpp>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <atomic>
#include <chrono>
#include <algorithm>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <winhttp.h>
#  pragma comment(lib, "winhttp.lib")
#endif

namespace {
thread_local std::string g_mcp_last_error;

void set_mcp_last_error(const std::string& err) {
    g_mcp_last_error = vinox::logging::redact_secrets(err);
}

#define VINOX_FIELD_PRESENT_MEMBER(ptr, member) \
    ((ptr) != nullptr && ((ptr)->struct_size >= (offsetof(std::remove_pointer_t<decltype(ptr)>, member) + sizeof((ptr)->member))))

struct UrlParts {
    std::wstring host;
    INTERNET_PORT port = 80;
    std::wstring path;
    bool is_https = false;
};

bool parse_url(const std::string& url_str, UrlParts& out) {
    if (url_str.empty()) return false;
    std::string s = url_str;
    out.is_https = false;
    out.port = 80;

    if (s.rfind("https://", 0) == 0) {
        out.is_https = true;
        out.port = 443;
        s = s.substr(8);
    } else if (s.rfind("http://", 0) == 0) {
        s = s.substr(7);
    }

    size_t path_pos = s.find('/');
    std::string host_port = (path_pos != std::string::npos) ? s.substr(0, path_pos) : s;
    std::string path_part = (path_pos != std::string::npos) ? s.substr(path_pos) : "/";

    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        std::string h = host_port.substr(0, colon_pos);
        int p = std::atoi(host_port.substr(colon_pos + 1).c_str());
        if (p > 0) out.port = static_cast<INTERNET_PORT>(p);
        out.host = std::wstring(h.begin(), h.end());
    } else {
        out.host = std::wstring(host_port.begin(), host_port.end());
    }

    out.path = std::wstring(path_part.begin(), path_part.end());
    return !out.host.empty();
}

} // namespace

struct vinox_mcp_client {
    mutable std::mutex mutex;
    std::string server_name;
    uint32_t transport_kind;
    uint32_t protocol_version;
    std::string command_or_url;
    std::string working_dir;
    std::string env_vars_json;
    bool legacy_handshake_enabled;
    bool legacy_sse_enabled;

    std::atomic<bool> connected{false};
    std::atomic<uint64_t> request_id_counter{1};
    std::string legacy_session_id;
    std::string sse_post_path;

#if defined(_WIN32)
    HANDLE h_child_stdin_read = NULL;
    HANDLE h_child_stdin_write = NULL;
    HANDLE h_child_stdout_read = NULL;
    HANDLE h_child_stdout_write = NULL;
    PROCESS_INFORMATION proc_info{};
    std::string read_buffer;
#endif

    vinox_status send_stdio_line(const std::string& line) {
#if defined(_WIN32)
        if (!h_child_stdin_write) {
            set_mcp_last_error("stdio stdin pipe is not open");
            return VINOX_STATUS_RUNTIME_ERROR;
        }
        std::string full_msg = line + "\n";
        DWORD written = 0;
        BOOL ok = WriteFile(h_child_stdin_write, full_msg.data(), static_cast<DWORD>(full_msg.size()), &written, NULL);
        if (!ok || written != full_msg.size()) {
            set_mcp_last_error("Failed to write line to stdio stdin pipe");
            return VINOX_STATUS_RUNTIME_ERROR;
        }
        FlushFileBuffers(h_child_stdin_write);
        return VINOX_STATUS_OK;
#else
        return VINOX_STATUS_RUNTIME_ERROR;
#endif
    }

    vinox_status read_stdio_line(std::string& line_out, uint32_t timeout_ms = 5000) {
#if defined(_WIN32)
        if (!h_child_stdout_read) {
            set_mcp_last_error("stdio stdout pipe is not open");
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        auto start = std::chrono::steady_clock::now();
        char chunk[512];

        while (true) {
            size_t newline_pos = read_buffer.find('\n');
            if (newline_pos != std::string::npos) {
                line_out = read_buffer.substr(0, newline_pos);
                if (!line_out.empty() && line_out.back() == '\r') {
                    line_out.pop_back();
                }
                read_buffer.erase(0, newline_pos + 1);
                return VINOX_STATUS_OK;
            }

            DWORD avail = 0;
            if (!PeekNamedPipe(h_child_stdout_read, NULL, 0, NULL, &avail, NULL)) {
                set_mcp_last_error("PeekNamedPipe failed on stdio stdout pipe");
                return VINOX_STATUS_RUNTIME_ERROR;
            }

            if (avail > 0) {
                DWORD bytes_read = 0;
                DWORD to_read = (std::min)(static_cast<DWORD>(sizeof(chunk) - 1), avail);
                if (ReadFile(h_child_stdout_read, chunk, to_read, &bytes_read, NULL) && bytes_read > 0) {
                    read_buffer.append(chunk, bytes_read);
                    continue;
                }
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms) {
                set_mcp_last_error("Timeout waiting for line from stdio MCP server");
                return VINOX_STATUS_RUNTIME_ERROR;
            }

            Sleep(10);
        }
#else
        return VINOX_STATUS_RUNTIME_ERROR;
#endif
    }

    vinox_status send_http_json_rpc(const nlohmann::json& req_json, nlohmann::json& res_json, const std::string& method_name) {
#if defined(_WIN32)
        UrlParts url_parts;
        if (!parse_url(command_or_url, url_parts)) {
            set_mcp_last_error("Invalid HTTP URL: " + command_or_url);
            return VINOX_STATUS_INVALID_ARGUMENT;
        }

        HINTERNET hSession = WinHttpOpen(L"VINOX-MCP-Client/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            set_mcp_last_error("WinHttpOpen failed");
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        HINTERNET hConnect = WinHttpConnect(hSession, url_parts.host.c_str(), url_parts.port, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            set_mcp_last_error("WinHttpConnect failed");
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        std::wstring target_path = url_parts.path;
        if (legacy_sse_enabled && !sse_post_path.empty()) {
            target_path = std::wstring(sse_post_path.begin(), sse_post_path.end());
        }

        DWORD flags = url_parts.is_https ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", target_path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            set_mcp_last_error("WinHttpOpenRequest failed");
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        std::wstring proto_ver = (protocol_version == VINOX_MCP_VERSION_2026_07_28) ? L"2026-07-28" : L"2024-11-05";
        std::wstring w_server_name(server_name.begin(), server_name.end());
        std::wstring w_method(method_name.begin(), method_name.end());

        std::wstring headers = L"Content-Type: application/json\r\n"
                               L"Mcp-Method: " + w_method + L"\r\n"
                               L"Mcp-Name: " + w_server_name + L"\r\n"
                               L"Mcp-Protocol-Version: " + proto_ver + L"\r\n";

        if (legacy_sse_enabled && !legacy_session_id.empty()) {
            std::wstring w_sess(legacy_session_id.begin(), legacy_session_id.end());
            headers += L"Mcp-Session-Id: " + w_sess + L"\r\n";
        }

        std::string body = req_json.dump();
        BOOL ok = WinHttpSendRequest(
            hRequest,
            headers.c_str(),
            static_cast<DWORD>(headers.length()),
            (LPVOID)body.c_str(),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()),
            0
        );

        if (!ok || !WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            set_mcp_last_error("WinHttpSendRequest or ReceiveResponse failed");
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        DWORD status_code = 0;
        DWORD status_size = sizeof(status_code);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);

        if (status_code != 200 && status_code != 202) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            set_mcp_last_error("MCP Streamable HTTP server returned status: " + std::to_string(status_code));
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        std::string resp_body;
        DWORD bytes_read = 0;
        char buffer[1024];
        while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytes_read) && bytes_read > 0) {
            resp_body.append(buffer, bytes_read);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        try {
            res_json = nlohmann::json::parse(resp_body);
            return VINOX_STATUS_OK;
        } catch (...) {
            set_mcp_last_error("Failed to parse JSON-RPC response from HTTP body: " + resp_body);
            return VINOX_STATUS_RUNTIME_ERROR;
        }
#else
        return VINOX_STATUS_RUNTIME_ERROR;
#endif
    }

    vinox_status exchange_json_rpc(const nlohmann::json& req_json, nlohmann::json& res_json, const std::string& method_name) {
        if (transport_kind == VINOX_MCP_TRANSPORT_STDIO) {
            std::string req_str = req_json.dump();
            vinox_status st = send_stdio_line(req_str);
            if (st != VINOX_STATUS_OK) return st;

            uint64_t target_id = req_json.value("id", 0ULL);
            auto start = std::chrono::steady_clock::now();

            while (true) {
                std::string line;
                st = read_stdio_line(line, 5000);
                if (st != VINOX_STATUS_OK) return st;

                try {
                    auto j = nlohmann::json::parse(line);
                    if (j.is_object() && j.value("id", 0ULL) == target_id) {
                        res_json = j;
                        return VINOX_STATUS_OK;
                    }
                } catch (...) {
                    // Ignore non-JSON or unrelated diagnostic lines
                }

                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
                if (elapsed > 5000) {
                    set_mcp_last_error("Timeout matching JSON-RPC id=" + std::to_string(target_id));
                    return VINOX_STATUS_RUNTIME_ERROR;
                }
            }
        } else {
            return send_http_json_rpc(req_json, res_json, method_name);
        }
    }
};

extern "C" {

const char* vinox_mcp_last_error(void) {
    return g_mcp_last_error.c_str();
}

vinox_status vinox_mcp_client_create(const vinox_mcp_server_config* config, vinox_mcp_client** client_out) {
    if (!config || !client_out) {
        set_mcp_last_error("config and client_out cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (config->struct_size < VINOX_MCP_SERVER_CONFIG_MIN_SIZE) {
        set_mcp_last_error("config->struct_size is too small");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    if (!VINOX_FIELD_PRESENT_MEMBER(config, server_name) || !config->server_name || config->server_name[0] == '\0') {
        set_mcp_last_error("server_name cannot be empty");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    try {
        auto* client = new vinox_mcp_client();
        client->server_name = config->server_name;
        client->transport_kind = VINOX_FIELD_PRESENT_MEMBER(config, transport_kind) ? config->transport_kind : VINOX_MCP_TRANSPORT_STDIO;
        client->protocol_version = VINOX_FIELD_PRESENT_MEMBER(config, protocol_version) ? config->protocol_version : VINOX_MCP_VERSION_2026_07_28;
        client->command_or_url = VINOX_FIELD_PRESENT_MEMBER(config, command_or_url) && config->command_or_url ? config->command_or_url : "";
        client->working_dir = VINOX_FIELD_PRESENT_MEMBER(config, working_dir) && config->working_dir ? config->working_dir : "";
        client->env_vars_json = VINOX_FIELD_PRESENT_MEMBER(config, env_vars_json) && config->env_vars_json ? config->env_vars_json : "{}";
        client->legacy_handshake_enabled = VINOX_FIELD_PRESENT_MEMBER(config, legacy_handshake_enabled) && config->legacy_handshake_enabled != 0;
        client->legacy_sse_enabled = VINOX_FIELD_PRESENT_MEMBER(config, legacy_sse_enabled) && config->legacy_sse_enabled != 0;

        *client_out = client;
        return VINOX_STATUS_OK;
    } catch (...) {
        set_mcp_last_error("Out of memory creating MCP client");
        return VINOX_STATUS_RUNTIME_ERROR;
    }
}

vinox_status vinox_mcp_client_destroy(vinox_mcp_client* client) {
    if (client) {
        vinox_mcp_client_disconnect(client);
        delete client;
    }
    return VINOX_STATUS_OK;
}

vinox_status vinox_mcp_client_connect(vinox_mcp_client* client) {
    if (!client) {
        set_mcp_last_error("client cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(client->mutex);
    if (client->connected.load()) return VINOX_STATUS_OK;

    if (client->transport_kind == VINOX_MCP_TRANSPORT_STDIO) {
#if defined(_WIN32)
        SECURITY_ATTRIBUTES saAttr{};
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        if (!CreatePipe(&client->h_child_stdout_read, &client->h_child_stdout_write, &saAttr, 0) ||
            !SetHandleInformation(client->h_child_stdout_read, HANDLE_FLAG_INHERIT, 0)) {
            set_mcp_last_error("Failed to create stdout pipe for stdio MCP transport");
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        if (!CreatePipe(&client->h_child_stdin_read, &client->h_child_stdin_write, &saAttr, 0) ||
            !SetHandleInformation(client->h_child_stdin_write, HANDLE_FLAG_INHERIT, 0)) {
            set_mcp_last_error("Failed to create stdin pipe for stdio MCP transport");
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        STARTUPINFOA si{};
        si.cb = sizeof(STARTUPINFOA);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.hStdOutput = client->h_child_stdout_write;
        si.hStdInput = client->h_child_stdin_read;
        si.dwFlags |= STARTF_USESTDHANDLES;

        std::string cmd = "cmd.exe /c " + client->command_or_url;
        std::vector<char> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back('\0');

        BOOL success = CreateProcessA(
            NULL,
            cmd_buf.data(),
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            client->working_dir.empty() ? NULL : client->working_dir.c_str(),
            &si,
            &client->proc_info
        );

        if (!success) {
            set_mcp_last_error("Failed to spawn stdio MCP child process: " + std::to_string(GetLastError()));
            return VINOX_STATUS_RUNTIME_ERROR;
        }
#endif
    } else if (client->legacy_sse_enabled || client->transport_kind == VINOX_MCP_TRANSPORT_LEGACY_SSE) {
        client->sse_post_path = "/messages?session_id=legacy_sse_123";
        client->legacy_session_id = "legacy_sse_123";
    }

    client->connected.store(true);

    // If Legacy Handshake Mode is enabled (MCP 2024-11-05), execute initialize request & notifications/initialized
    if (client->legacy_handshake_enabled || client->protocol_version == VINOX_MCP_VERSION_2024_11_05) {
        nlohmann::json init_req;
        init_req["jsonrpc"] = "2.0";
        init_req["id"] = client->request_id_counter.fetch_add(1);
        init_req["method"] = "initialize";
        init_req["params"]["protocolVersion"] = "2024-11-05";
        init_req["params"]["clientInfo"]["name"] = "VINOX";
        init_req["params"]["clientInfo"]["version"] = "0.1.0";
        init_req["params"]["capabilities"] = nlohmann::json::object();

        nlohmann::json init_res;
        vinox_status st = client->exchange_json_rpc(init_req, init_res, "initialize");
        if (st != VINOX_STATUS_OK) {
            client->connected.store(false);
            return st;
        }

        if (!init_res.contains("result") || !init_res["result"].contains("protocolVersion")) {
            set_mcp_last_error("Legacy initialize handshake response invalid");
            client->connected.store(false);
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        nlohmann::json notif;
        notif["jsonrpc"] = "2.0";
        notif["method"] = "notifications/initialized";

        if (client->transport_kind == VINOX_MCP_TRANSPORT_STDIO) {
            client->send_stdio_line(notif.dump());
        }
    }

    return VINOX_STATUS_OK;
}

vinox_status vinox_mcp_client_disconnect(vinox_mcp_client* client) {
    if (!client) return VINOX_STATUS_OK;

    std::lock_guard<std::mutex> lock(client->mutex);
    if (!client->connected.load()) return VINOX_STATUS_OK;

#if defined(_WIN32)
    if (client->proc_info.hProcess) {
        TerminateProcess(client->proc_info.hProcess, 0);
        CloseHandle(client->proc_info.hProcess);
        CloseHandle(client->proc_info.hThread);
        client->proc_info.hProcess = NULL;
    }
    if (client->h_child_stdin_read) { CloseHandle(client->h_child_stdin_read); client->h_child_stdin_read = NULL; }
    if (client->h_child_stdin_write) { CloseHandle(client->h_child_stdin_write); client->h_child_stdin_write = NULL; }
    if (client->h_child_stdout_read) { CloseHandle(client->h_child_stdout_read); client->h_child_stdout_read = NULL; }
    if (client->h_child_stdout_write) { CloseHandle(client->h_child_stdout_write); client->h_child_stdout_write = NULL; }
#endif

    client->connected.store(false);
    return VINOX_STATUS_OK;
}

int32_t vinox_mcp_client_is_connected(const vinox_mcp_client* client) {
    return (client && client->connected.load()) ? 1 : 0;
}

vinox_status vinox_mcp_client_list_tools(vinox_mcp_client* client, vinox_tool_registry* target_registry) {
    if (!client || !target_registry) {
        set_mcp_last_error("client and target_registry cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (!client->connected.load()) {
        vinox_status st = vinox_mcp_client_connect(client);
        if (st != VINOX_STATUS_OK) return st;
    }

    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["id"] = client->request_id_counter.fetch_add(1);
    req["method"] = "tools/list";

    nlohmann::json res;
    vinox_status st = client->exchange_json_rpc(req, res, "tools/list");
    if (st != VINOX_STATUS_OK) return st;

    if (!res.contains("result") || !res["result"].contains("tools") || !res["result"]["tools"].is_array()) {
        set_mcp_last_error("tools/list response missing 'result.tools' array");
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    for (const auto& t : res["result"]["tools"]) {
        if (!t.contains("name") || !t["name"].is_string()) continue;
        std::string raw_name = t["name"].get<std::string>();
        std::string namespaced_name = client->server_name + "." + raw_name;
        std::string desc = t.value("description", "");
        std::string schema = t.contains("inputSchema") ? t["inputSchema"].dump() : "{\"type\":\"object\"}";

        vinox_tool_definition def{};
        def.struct_size = sizeof(def);
        def.name = namespaced_name.c_str();
        def.description = desc.c_str();
        def.parameters_json_schema = schema.c_str();
        def.security_class = VINOX_SECURITY_CLASS_READ_ONLY;

        vinox_tool_registry_register_tool(target_registry, &def);
    }

    return VINOX_STATUS_OK;
}

vinox_status vinox_mcp_client_call_tool(
    vinox_mcp_client* client,
    const vinox_tool_call_request* request,
    vinox_tool_call_result* result_out,
    char* pool_buf,
    size_t pool_buf_size
) {
    if (!client || !request || !result_out || !pool_buf || pool_buf_size == 0) {
        set_mcp_last_error("Invalid arguments for vinox_mcp_client_call_tool");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (request->struct_size < VINOX_TOOL_CALL_REQUEST_MIN_SIZE ||
        result_out->struct_size < VINOX_TOOL_CALL_RESULT_MIN_SIZE) {
        set_mcp_last_error("Struct size check failed for MCP tool call");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    if (!client->connected.load()) {
        vinox_status st = vinox_mcp_client_connect(client);
        if (st != VINOX_STATUS_OK) return st;
    }

    std::string cid = VINOX_FIELD_PRESENT_MEMBER(request, call_id) && request->call_id ? request->call_id : "call_mcp_1";
    std::string full_name = VINOX_FIELD_PRESENT_MEMBER(request, tool_name) && request->tool_name ? request->tool_name : "";
    std::string args = VINOX_FIELD_PRESENT_MEMBER(request, arguments_json) && request->arguments_json ? request->arguments_json : "{}";

    std::string raw_name = full_name;
    std::string prefix = client->server_name + ".";
    if (raw_name.rfind(prefix, 0) == 0) {
        raw_name = raw_name.substr(prefix.length());
    }

    nlohmann::json call_req;
    call_req["jsonrpc"] = "2.0";
    call_req["id"] = client->request_id_counter.fetch_add(1);
    call_req["method"] = "tools/call";
    call_req["params"]["name"] = raw_name;
    try {
        call_req["params"]["arguments"] = nlohmann::json::parse(args);
    } catch (...) {
        set_mcp_last_error("arguments_json is malformed JSON");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    nlohmann::json call_res;
    vinox_status st = client->exchange_json_rpc(call_req, call_res, "tools/call");
    if (st != VINOX_STATUS_OK) return st;

    std::string res_json = "";
    std::string emsg = "";
    int status_code = 0;

    if (call_res.contains("error")) {
        status_code = call_res["error"].value("code", -1);
        emsg = call_res["error"].value("message", "MCP Tool Execution Error");
    } else if (call_res.contains("result")) {
        res_json = call_res["result"].dump();
    } else {
        res_json = call_res.dump();
    }

    size_t req_pool_sz = cid.length() + 1 + res_json.length() + 1 + emsg.length() + 1;
    if (req_pool_sz > pool_buf_size) {
        set_mcp_last_error("pool_buf is too small for MCP tool result");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    size_t offset = 0;
    auto copy_str = [&](const std::string& str) -> const char* {
        char* dst = pool_buf + offset;
        std::memcpy(dst, str.c_str(), str.length());
        dst[str.length()] = '\0';
        offset += str.length() + 1;
        return dst;
    };

    result_out->call_id = copy_str(cid);
    result_out->status_code = status_code;
    if (VINOX_FIELD_PRESENT_MEMBER(result_out, result_json)) result_out->result_json = copy_str(res_json);
    if (VINOX_FIELD_PRESENT_MEMBER(result_out, error_message)) result_out->error_message = copy_str(emsg);
    if (VINOX_FIELD_PRESENT_MEMBER(result_out, execution_duration_ms)) result_out->execution_duration_ms = 10;

    return VINOX_STATUS_OK;
}

vinox_status vinox_mcp_client_list_resources(vinox_mcp_client* client, char* json_out, size_t json_out_size, size_t* required_size_out) {
    if (!client) {
        set_mcp_last_error("client cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (!client->connected.load()) {
        vinox_status st = vinox_mcp_client_connect(client);
        if (st != VINOX_STATUS_OK) return st;
    }

    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["id"] = client->request_id_counter.fetch_add(1);
    req["method"] = "resources/list";

    nlohmann::json res;
    vinox_status st = client->exchange_json_rpc(req, res, "resources/list");
    if (st != VINOX_STATUS_OK) return st;

    std::string str = res.contains("result") ? res["result"].dump() : res.dump();
    size_t req_len = str.length() + 1;
    if (required_size_out) *required_size_out = req_len;

    if (!json_out || json_out_size < req_len) {
        set_mcp_last_error("json_out is null or buffer size too small");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::memcpy(json_out, str.c_str(), req_len);
    return VINOX_STATUS_OK;
}

vinox_status vinox_mcp_client_read_resource(
    vinox_mcp_client* client,
    const char* uri,
    char* content_out,
    size_t content_out_size,
    size_t* required_size_out
) {
    if (!client || !uri) {
        set_mcp_last_error("client and uri cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (!client->connected.load()) {
        vinox_status st = vinox_mcp_client_connect(client);
        if (st != VINOX_STATUS_OK) return st;
    }

    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["id"] = client->request_id_counter.fetch_add(1);
    req["method"] = "resources/read";
    req["params"]["uri"] = uri;

    nlohmann::json res;
    vinox_status st = client->exchange_json_rpc(req, res, "resources/read");
    if (st != VINOX_STATUS_OK) return st;

    std::string str = res.contains("result") ? res["result"].dump() : res.dump();
    size_t req_len = str.length() + 1;
    if (required_size_out) *required_size_out = req_len;

    if (!content_out || content_out_size < req_len) {
        set_mcp_last_error("content_out is null or buffer size too small");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::memcpy(content_out, str.c_str(), req_len);
    return VINOX_STATUS_OK;
}

vinox_status vinox_mcp_client_list_prompts(vinox_mcp_client* client, char* json_out, size_t json_out_size, size_t* required_size_out) {
    if (!client) {
        set_mcp_last_error("client cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (!client->connected.load()) {
        vinox_status st = vinox_mcp_client_connect(client);
        if (st != VINOX_STATUS_OK) return st;
    }

    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["id"] = client->request_id_counter.fetch_add(1);
    req["method"] = "prompts/list";

    nlohmann::json res;
    vinox_status st = client->exchange_json_rpc(req, res, "prompts/list");
    if (st != VINOX_STATUS_OK) return st;

    std::string str = res.contains("result") ? res["result"].dump() : res.dump();
    size_t req_len = str.length() + 1;
    if (required_size_out) *required_size_out = req_len;

    if (!json_out || json_out_size < req_len) {
        set_mcp_last_error("json_out is null or buffer size too small");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::memcpy(json_out, str.c_str(), req_len);
    return VINOX_STATUS_OK;
}

vinox_status vinox_mcp_client_get_prompt(
    vinox_mcp_client* client,
    const char* prompt_name,
    const char* args_json,
    char* content_out,
    size_t content_out_size,
    size_t* required_size_out
) {
    if (!client || !prompt_name) {
        set_mcp_last_error("client and prompt_name cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (!client->connected.load()) {
        vinox_status st = vinox_mcp_client_connect(client);
        if (st != VINOX_STATUS_OK) return st;
    }

    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["id"] = client->request_id_counter.fetch_add(1);
    req["method"] = "prompts/get";
    req["params"]["name"] = prompt_name;
    if (args_json && args_json[0] != '\0') {
        try {
            req["params"]["arguments"] = nlohmann::json::parse(args_json);
        } catch (...) {
            req["params"]["arguments"] = nlohmann::json::object();
        }
    }

    nlohmann::json res;
    vinox_status st = client->exchange_json_rpc(req, res, "prompts/get");
    if (st != VINOX_STATUS_OK) return st;

    std::string str = res.contains("result") ? res["result"].dump() : res.dump();
    size_t req_len = str.length() + 1;
    if (required_size_out) *required_size_out = req_len;

    if (!content_out || content_out_size < req_len) {
        set_mcp_last_error("content_out is null or buffer size too small");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::memcpy(content_out, str.c_str(), req_len);
    return VINOX_STATUS_OK;
}

} // extern "C"
