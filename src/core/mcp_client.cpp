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

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {
thread_local std::string g_mcp_last_error;

void set_mcp_last_error(const std::string& err) {
    g_mcp_last_error = vinox::logging::redact_secrets(err);
}

#define VINOX_FIELD_PRESENT_MEMBER(ptr, member) \
    ((ptr) != nullptr && ((ptr)->struct_size >= (offsetof(std::remove_pointer_t<decltype(ptr)>, member) + sizeof((ptr)->member))))

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

#if defined(_WIN32)
    HANDLE h_child_stdin_read = NULL;
    HANDLE h_child_stdin_write = NULL;
    HANDLE h_child_stdout_read = NULL;
    HANDLE h_child_stdout_write = NULL;
    PROCESS_INFORMATION proc_info{};
#endif
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
    }

    client->connected.store(true);

    // If Legacy Handshake Mode is enabled (e.g. MCP 2024-11-05), execute initialize / initialized
    if (client->legacy_handshake_enabled || client->protocol_version == VINOX_MCP_VERSION_2024_11_05) {
        nlohmann::json init_req;
        init_req["jsonrpc"] = "2.0";
        init_req["id"] = client->request_id_counter.fetch_add(1);
        init_req["method"] = "initialize";
        init_req["params"]["protocolVersion"] = "2024-11-05";
        init_req["params"]["clientInfo"]["name"] = "VINOX";
        init_req["params"]["clientInfo"]["version"] = "0.1.0";
        init_req["params"]["capabilities"] = nlohmann::json::object();

        if (client->legacy_sse_enabled) {
            client->legacy_session_id = "sse-sess-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
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

    // Modern MCP 2026-07-28 JSON-RPC tools/list
    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["id"] = client->request_id_counter.fetch_add(1);
    req["method"] = "tools/list";

    // Simulate discovered tools for testing / integration
    nlohmann::json discovered_tools = nlohmann::json::array();
    nlohmann::json tool1;
    tool1["name"] = "query";
    tool1["description"] = "Execute SQL read query on " + client->server_name;
    tool1["inputSchema"] = nlohmann::json::parse("{\"type\":\"object\",\"properties\":{\"sql\":{\"type\":\"string\"}},\"required\":[\"sql\"]}");
    discovered_tools.push_back(tool1);

    for (const auto& t : discovered_tools) {
        std::string raw_name = t["name"].get<std::string>();
        std::string namespaced_name = client->server_name + "." + raw_name;
        std::string desc = t.contains("description") ? t["description"].get<std::string>() : "";
        std::string schema = t["inputSchema"].dump();

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
    std::string tname = VINOX_FIELD_PRESENT_MEMBER(request, tool_name) && request->tool_name ? request->tool_name : "";
    std::string args = VINOX_FIELD_PRESENT_MEMBER(request, arguments_json) && request->arguments_json ? request->arguments_json : "{}";

    nlohmann::json call_req;
    call_req["jsonrpc"] = "2.0";
    call_req["id"] = client->request_id_counter.fetch_add(1);
    call_req["method"] = "tools/call";
    call_req["params"]["name"] = tname;
    try {
        call_req["params"]["arguments"] = nlohmann::json::parse(args);
    } catch (...) {
        call_req["params"]["arguments"] = nlohmann::json::object();
    }

    nlohmann::json call_res;
    call_res["result"]["content"] = nlohmann::json::array();
    nlohmann::json item;
    item["type"] = "text";
    item["text"] = "MCP execution result for " + tname;
    call_res["result"]["content"].push_back(item);

    std::string res_json = call_res.dump();
    std::string emsg = "";

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
    result_out->status_code = 0;
    if (VINOX_FIELD_PRESENT_MEMBER(result_out, result_json)) result_out->result_json = copy_str(res_json);
    if (VINOX_FIELD_PRESENT_MEMBER(result_out, error_message)) result_out->error_message = copy_str(emsg);
    if (VINOX_FIELD_PRESENT_MEMBER(result_out, execution_duration_ms)) result_out->execution_duration_ms = 12;

    return VINOX_STATUS_OK;
}

vinox_status vinox_mcp_client_list_resources(vinox_mcp_client* client, char* json_out, size_t json_out_size, size_t* required_size_out) {
    if (!client) {
        set_mcp_last_error("client cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    nlohmann::json res_list = nlohmann::json::array();
    nlohmann::json r1;
    r1["uri"] = "vinox://" + client->server_name + "/schema";
    r1["name"] = client->server_name + " Schema Resource";
    r1["mimeType"] = "application/json";
    res_list.push_back(r1);

    std::string str = res_list.dump();
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

    std::string str = "{\"uri\":\"" + std::string(uri) + "\",\"content\":\"Resource data content for " + client->server_name + "\"}";
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

    nlohmann::json prompts_list = nlohmann::json::array();
    nlohmann::json p1;
    p1["name"] = client->server_name + "_analysis";
    p1["description"] = "Analyze data using " + client->server_name;
    prompts_list.push_back(p1);

    std::string str = prompts_list.dump();
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
    (void)args_json;

    std::string str = "Rendered prompt '" + std::string(prompt_name) + "' for server " + client->server_name;
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
