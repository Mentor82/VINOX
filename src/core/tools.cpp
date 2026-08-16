#include "vinox/tools.h"
#include "vinox/tools.hpp"
#include "vinox/logging.hpp"
#include "vinox/logging.h"

#include <nlohmann/json.hpp>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace {
thread_local std::string g_tools_last_error;

void set_tools_last_error(const std::string& err) {
    g_tools_last_error = vinox::logging::redact_secrets(err);
}

// Minimal macro for struct field checks matching existing C-ABI pattern
#define VINOX_FIELD_PRESENT_MEMBER(ptr, member) \
    ((ptr) != nullptr && ((ptr)->struct_size >= (offsetof(std::remove_pointer_t<decltype(ptr)>, member) + sizeof((ptr)->member))))

struct ToolEntry {
    std::string name;
    std::string description;
    std::string parameters_json_schema;
    uint32_t security_class;
};

struct PolicyRule {
    std::string pattern;
    uint32_t max_security_class;
    uint32_t approval_mode;
};

bool match_pattern(const std::string& pattern, const std::string& name) {
    if (pattern == "*" || pattern == name) return true;
    if (pattern.length() > 2 && pattern.back() == '*') {
        std::string prefix = pattern.substr(0, pattern.length() - 1);
        if (name.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

} // namespace

struct vinox_tool_registry {
    mutable std::mutex mutex;
    std::unordered_map<std::string, ToolEntry> tools;
};

struct vinox_policy_engine {
    mutable std::mutex mutex;
    std::vector<PolicyRule> rules;
};

extern "C" {

const char* vinox_tools_last_error(void) {
    return g_tools_last_error.c_str();
}

vinox_status vinox_tool_registry_create(vinox_tool_registry** registry_out) {
    if (!registry_out) {
        set_tools_last_error("registry_out cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }
    try {
        *registry_out = new vinox_tool_registry();
        return VINOX_STATUS_OK;
    } catch (...) {
        set_tools_last_error("Out of memory creating tool registry");
        return VINOX_STATUS_RUNTIME_ERROR;
    }
}

vinox_status vinox_tool_registry_destroy(vinox_tool_registry* registry) {
    if (registry) {
        delete registry;
    }
    return VINOX_STATUS_OK;
}

vinox_status vinox_tool_registry_register_tool(vinox_tool_registry* registry, const vinox_tool_definition* tool_def) {
    if (!registry || !tool_def) {
        set_tools_last_error("registry and tool_def cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (tool_def->struct_size < VINOX_TOOL_DEFINITION_MIN_SIZE) {
        set_tools_last_error("tool_def->struct_size is too small");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    if (!VINOX_FIELD_PRESENT_MEMBER(tool_def, name) || !tool_def->name || tool_def->name[0] == '\0') {
        set_tools_last_error("tool name cannot be empty");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::string name = tool_def->name;
    std::string desc = VINOX_FIELD_PRESENT_MEMBER(tool_def, description) && tool_def->description ? tool_def->description : "";
    std::string schema_str = VINOX_FIELD_PRESENT_MEMBER(tool_def, parameters_json_schema) && tool_def->parameters_json_schema ? tool_def->parameters_json_schema : "{}";
    uint32_t sec_class = VINOX_FIELD_PRESENT_MEMBER(tool_def, security_class) ? tool_def->security_class : VINOX_SECURITY_CLASS_READ_ONLY;

    if (sec_class > VINOX_SECURITY_CLASS_ADMIN) {
        set_tools_last_error("Invalid security class tier");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    // Validate parameters JSON schema syntax
    try {
        auto parsed_schema = nlohmann::json::parse(schema_str);
    } catch (const std::exception& e) {
        set_tools_last_error(std::string("Invalid JSON schema for tool '") + name + "': " + e.what());
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(registry->mutex);
    registry->tools[name] = ToolEntry{name, desc, schema_str, sec_class};
    return VINOX_STATUS_OK;
}

vinox_status vinox_tool_registry_unregister_tool(vinox_tool_registry* registry, const char* tool_name) {
    if (!registry || !tool_name) {
        set_tools_last_error("registry and tool_name cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(registry->mutex);
    registry->tools.erase(tool_name);
    return VINOX_STATUS_OK;
}

vinox_status vinox_tool_registry_find_tool(
    const vinox_tool_registry* registry,
    const char* tool_name,
    vinox_tool_definition* tool_def_out,
    char* pool_buf,
    size_t pool_buf_size
) {
    if (!registry || !tool_name || !tool_def_out || !pool_buf || pool_buf_size == 0) {
        set_tools_last_error("Invalid arguments for vinox_tool_registry_find_tool");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (tool_def_out->struct_size < VINOX_TOOL_DEFINITION_MIN_SIZE) {
        set_tools_last_error("tool_def_out->struct_size is too small");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    std::lock_guard<std::mutex> lock(registry->mutex);
    auto it = registry->tools.find(tool_name);
    if (it == registry->tools.end()) {
        set_tools_last_error(std::string("Tool not found: ") + tool_name);
        return VINOX_STATUS_NOT_FOUND;
    }

    const auto& entry = it->second;
    size_t req_pool_sz = entry.name.length() + 1 + entry.description.length() + 1 + entry.parameters_json_schema.length() + 1;
    if (req_pool_sz > pool_buf_size) {
        set_tools_last_error("pool_buf is too small to copy tool definition");
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

    const char* n = copy_str(entry.name);
    const char* d = copy_str(entry.description);
    const char* s = copy_str(entry.parameters_json_schema);

    tool_def_out->name = n;
    if (VINOX_FIELD_PRESENT_MEMBER(tool_def_out, description)) tool_def_out->description = d;
    if (VINOX_FIELD_PRESENT_MEMBER(tool_def_out, parameters_json_schema)) tool_def_out->parameters_json_schema = s;
    if (VINOX_FIELD_PRESENT_MEMBER(tool_def_out, security_class)) tool_def_out->security_class = entry.security_class;

    return VINOX_STATUS_OK;
}

vinox_status vinox_tool_registry_validate_arguments(
    const vinox_tool_registry* registry,
    const char* tool_name,
    const char* args_json,
    char* err_buf,
    size_t err_buf_size
) {
    if (!registry || !tool_name || !args_json) {
        set_tools_last_error("registry, tool_name, and args_json cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (strlen(args_json) > 262144) {
        set_tools_last_error("Payload exceeds maximum bounded payload size of 262144 bytes (256 KB)");
        if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "Payload exceeds maximum bounded payload size of 262144 bytes (256 KB)");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::string name_str = tool_name;
    ToolEntry entry;
    {
        std::lock_guard<std::mutex> lock(registry->mutex);
        auto it = registry->tools.find(name_str);
        if (it == registry->tools.end()) {
            set_tools_last_error("Tool not found: " + name_str);
            if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "Tool not found: %s", tool_name);
            return VINOX_STATUS_NOT_FOUND;
        }
        entry = it->second;
    }

    nlohmann::json args_j;
    try {
        args_j = nlohmann::json::parse(args_json);
    } catch (const std::exception& e) {
        std::string emsg = std::string("Malformed arguments JSON: ") + e.what();
        set_tools_last_error(emsg);
        if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "%s", emsg.c_str());
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    nlohmann::json schema_j;
    try {
        schema_j = nlohmann::json::parse(entry.parameters_json_schema);
    } catch (...) {
        schema_j = nlohmann::json::object();
    }

    // Check additionalProperties: false
    bool allow_additional = true;
    if (schema_j.contains("additionalProperties") && schema_j["additionalProperties"].is_boolean()) {
        allow_additional = schema_j["additionalProperties"].get<bool>();
    }

    if (!allow_additional && schema_j.contains("properties") && schema_j["properties"].is_object()) {
        const auto& props = schema_j["properties"];
        for (auto it = args_j.begin(); it != args_j.end(); ++it) {
            if (!props.contains(it.key())) {
                std::string emsg = "Additional property '" + it.key() + "' is forbidden by schema";
                set_tools_last_error(emsg);
                if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "%s", emsg.c_str());
                return VINOX_STATUS_INVALID_ARGUMENT;
            }
        }
    }

    // Check required properties if specified in schema
    if (schema_j.contains("required") && schema_j["required"].is_array()) {
        for (const auto& req : schema_j["required"]) {
            if (req.is_string()) {
                std::string req_key = req.get<std::string>();
                if (!args_j.contains(req_key)) {
                    std::string emsg = "Missing required parameter: " + req_key;
                    set_tools_last_error(emsg);
                    if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "%s", emsg.c_str());
                    return VINOX_STATUS_INVALID_ARGUMENT;
                }
            }
        }
    }

    // Check parameter type matching & enum constraints if properties defined in schema
    if (schema_j.contains("properties") && schema_j["properties"].is_object()) {
        const auto& props = schema_j["properties"];
        for (auto it = args_j.begin(); it != args_j.end(); ++it) {
            std::string key = it.key();
            if (props.contains(key) && props[key].is_object()) {
                const auto& prop_spec = props[key];
                if (prop_spec.contains("type") && prop_spec["type"].is_string()) {
                    std::string expected_type = prop_spec["type"].get<std::string>();
                    const auto& val = it.value();
                    bool valid = true;
                    if (expected_type == "string" && !val.is_string()) valid = false;
                    else if (expected_type == "number" && !val.is_number()) valid = false;
                    else if (expected_type == "integer" && !val.is_number_integer()) valid = false;
                    else if (expected_type == "boolean" && !val.is_boolean()) valid = false;
                    else if (expected_type == "array" && !val.is_array()) valid = false;
                    else if (expected_type == "object" && !val.is_object()) valid = false;

                    if (!valid) {
                        std::string emsg = "Parameter '" + key + "' expected type '" + expected_type + "'";
                        set_tools_last_error(emsg);
                        if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "%s", emsg.c_str());
                        return VINOX_STATUS_INVALID_ARGUMENT;
                    }
                }

                if (prop_spec.contains("enum") && prop_spec["enum"].is_array()) {
                    const auto& enum_arr = prop_spec["enum"];
                    bool enum_match = false;
                    for (const auto& enum_val : enum_arr) {
                        if (enum_val == it.value()) {
                            enum_match = true;
                            break;
                        }
                    }
                    if (!enum_match) {
                        std::string emsg = "Value for parameter '" + key + "' is not one of the allowed enum values";
                        set_tools_last_error(emsg);
                        if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "%s", emsg.c_str());
                        return VINOX_STATUS_INVALID_ARGUMENT;
                    }
                }
            }
        }
    }

    return VINOX_STATUS_OK;
}

vinox_status vinox_policy_engine_create(vinox_policy_engine** engine_out) {
    if (!engine_out) {
        set_tools_last_error("engine_out cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }
    try {
        *engine_out = new vinox_policy_engine();
        return VINOX_STATUS_OK;
    } catch (...) {
        set_tools_last_error("Out of memory creating policy engine");
        return VINOX_STATUS_RUNTIME_ERROR;
    }
}

vinox_status vinox_policy_engine_destroy(vinox_policy_engine* engine) {
    if (engine) {
        delete engine;
    }
    return VINOX_STATUS_OK;
}

vinox_status vinox_policy_engine_set_rule(
    vinox_policy_engine* engine,
    const char* tool_name_pattern,
    uint32_t max_security_class,
    uint32_t approval_mode
) {
    if (!engine || !tool_name_pattern) {
        set_tools_last_error("engine and tool_name_pattern cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (max_security_class > VINOX_SECURITY_CLASS_ADMIN) {
        set_tools_last_error("Invalid max_security_class tier");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (approval_mode == VINOX_APPROVAL_DENIED || approval_mode > VINOX_APPROVAL_APPROVED_PERMANENT) {
        set_tools_last_error("Invalid approval_mode value");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(engine->mutex);
    engine->rules.push_back(PolicyRule{tool_name_pattern, max_security_class, approval_mode});
    return VINOX_STATUS_OK;
}

vinox_status vinox_policy_engine_evaluate(
    const vinox_policy_engine* engine,
    const vinox_tool_call_request* request,
    const vinox_tool_definition* tool_def,
    vinox_policy_decision* decision_out,
    char* reason_buf,
    size_t reason_buf_size
) {
    if (!engine || !request || !tool_def || !decision_out) {
        set_tools_last_error("engine, request, tool_def, and decision_out cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (request->struct_size < VINOX_TOOL_CALL_REQUEST_MIN_SIZE ||
        tool_def->struct_size < VINOX_TOOL_DEFINITION_MIN_SIZE ||
        decision_out->struct_size < VINOX_POLICY_DECISION_MIN_SIZE) {
        set_tools_last_error("Struct size check failed for policy evaluation");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    std::string req_tool_name = VINOX_FIELD_PRESENT_MEMBER(request, tool_name) && request->tool_name ? request->tool_name : "";
    std::string def_tool_name = VINOX_FIELD_PRESENT_MEMBER(tool_def, name) && tool_def->name ? tool_def->name : "";

    if (req_tool_name.empty() || req_tool_name != def_tool_name) {
        std::string emsg = "Tool request name '" + req_tool_name + "' does not match tool definition name '" + def_tool_name + "'";
        set_tools_last_error(emsg);
        decision_out->allowed = 0;
        if (VINOX_FIELD_PRESENT_MEMBER(decision_out, approval_mode)) decision_out->approval_mode = VINOX_APPROVAL_DENIED;
        if (reason_buf && reason_buf_size > 0) {
            snprintf(reason_buf, reason_buf_size, "%s", emsg.c_str());
            if (VINOX_FIELD_PRESENT_MEMBER(decision_out, reason)) decision_out->reason = reason_buf;
        }
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    uint32_t sec_class = VINOX_FIELD_PRESENT_MEMBER(tool_def, security_class) ? tool_def->security_class : VINOX_SECURITY_CLASS_READ_ONLY;

    std::lock_guard<std::mutex> lock(engine->mutex);
    bool allowed = false;
    uint32_t app_mode = VINOX_APPROVAL_DENIED;
    std::string reason = "Default-deny policy: no rule allowed tool execution";

    // Evaluate matching rules (last matching rule takes precedence if set)
    for (const auto& rule : engine->rules) {
        if (match_pattern(rule.pattern, req_tool_name)) {
            if (sec_class <= rule.max_security_class) {
                allowed = (rule.approval_mode != VINOX_APPROVAL_DENIED);
                app_mode = rule.approval_mode;
                reason = "Matched policy rule '" + rule.pattern + "'";
            } else {
                allowed = false;
                app_mode = VINOX_APPROVAL_DENIED;
                reason = "Security class tier " + std::to_string(sec_class) + " exceeds allowed tier " + std::to_string(rule.max_security_class);
            }
        }
    }

    decision_out->allowed = allowed ? 1 : 0;
    if (VINOX_FIELD_PRESENT_MEMBER(decision_out, approval_mode)) decision_out->approval_mode = app_mode;

    if (reason_buf && reason_buf_size > 0) {
        snprintf(reason_buf, reason_buf_size, "%s", reason.c_str());
        if (VINOX_FIELD_PRESENT_MEMBER(decision_out, reason)) decision_out->reason = reason_buf;
    }

    return VINOX_STATUS_OK;
}

vinox_status vinox_tools_format_openai_schema(
    const vinox_tool_registry* registry,
    char* output_buf,
    size_t output_buf_size,
    size_t* required_size_out
) {
    if (!registry) {
        set_tools_last_error("registry cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    nlohmann::json tools_arr = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lock(registry->mutex);
        for (const auto& kv : registry->tools) {
            const auto& t = kv.second;
            nlohmann::json func_obj;
            func_obj["name"] = t.name;
            func_obj["description"] = t.description;
            try {
                func_obj["parameters"] = nlohmann::json::parse(t.parameters_json_schema);
            } catch (...) {
                func_obj["parameters"] = nlohmann::json::object();
            }

            nlohmann::json tool_obj;
            tool_obj["type"] = "function";
            tool_obj["function"] = func_obj;
            tools_arr.push_back(tool_obj);
        }
    }

    std::string str = tools_arr.dump();
    size_t req_len = str.length() + 1;
    if (required_size_out) *required_size_out = req_len;

    if (!output_buf || output_buf_size < req_len) {
        set_tools_last_error("output_buf is null or buffer size too small");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::memcpy(output_buf, str.c_str(), req_len);
    return VINOX_STATUS_OK;
}

vinox_status vinox_tools_parse_openai_tool_call(
    const char* openai_tool_call_json,
    vinox_tool_call_request* request_out,
    char* pool_buf,
    size_t pool_buf_size
) {
    if (!openai_tool_call_json || !request_out || !pool_buf || pool_buf_size == 0) {
        set_tools_last_error("Invalid arguments for vinox_tools_parse_openai_tool_call");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (request_out->struct_size < VINOX_TOOL_CALL_REQUEST_MIN_SIZE) {
        set_tools_last_error("request_out->struct_size is too small");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(openai_tool_call_json);
    } catch (const std::exception& e) {
        set_tools_last_error(std::string("Invalid OpenAI tool call JSON: ") + e.what());
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::string call_id = j.contains("id") && j["id"].is_string() ? j["id"].get<std::string>() : "call_unknown";
    std::string tool_name;
    std::string args_json = "{}";

    if (j.contains("function") && j["function"].is_object()) {
        const auto& fn = j["function"];
        if (fn.contains("name") && fn["name"].is_string()) tool_name = fn["name"].get<std::string>();
        if (fn.contains("arguments")) {
            if (fn["arguments"].is_string()) args_json = fn["arguments"].get<std::string>();
            else if (fn["arguments"].is_object()) args_json = fn["arguments"].dump();
        }
    } else if (j.contains("name") && j["name"].is_string()) {
        tool_name = j["name"].get<std::string>();
        if (j.contains("arguments")) {
            if (j["arguments"].is_string()) args_json = j["arguments"].get<std::string>();
            else if (j["arguments"].is_object()) args_json = j["arguments"].dump();
        }
    }

    if (tool_name.empty()) {
        set_tools_last_error("OpenAI tool call missing function name");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    size_t req_pool_sz = call_id.length() + 1 + tool_name.length() + 1 + args_json.length() + 1;
    if (req_pool_sz > pool_buf_size) {
        set_tools_last_error("pool_buf is too small to copy parsed OpenAI tool call");
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

    const char* cid = copy_str(call_id);
    const char* tname = copy_str(tool_name);
    const char* args = copy_str(args_json);

    request_out->call_id = cid;
    request_out->tool_name = tname;
    request_out->arguments_json = args;

    return VINOX_STATUS_OK;
}

} // extern "C"
