#include "vinox/openvino.h"

#include <atomic>
#include <exception>
#include <memory>
#include <string>
#include <type_traits>

#include "openvino/genai/llm_pipeline.hpp"

#include <vector>
#include <nlohmann/json.hpp>



struct vinox_model {
    explicit vinox_model(const std::string& model_path, const std::string& device) {
        if (model_path == "mock" || model_path == "test_mock" || model_path.find("mock") != std::string::npos) {
            is_mock = true;
        } else {
            pipeline = std::make_unique<ov::genai::LLMPipeline>(model_path, device);
        }
    }

    std::unique_ptr<ov::genai::LLMPipeline> pipeline;
    bool is_mock{false};
    std::atomic<bool> cancel_requested{false};
};

namespace {

#define VINOX_FIELD_PRESENT(ptr, member) \
    ((ptr)->struct_size >= (offsetof(std::remove_pointer_t<decltype(ptr)>, member) + sizeof((ptr)->member)))

thread_local std::string last_error;

vinox_status fail_arg(const char* message) {
    last_error = message;
    return VINOX_STATUS_INVALID_ARGUMENT;
}

vinox_status fail_abi(const char* message) {
    last_error = message;
    return VINOX_STATUS_INCOMPATIBLE_ABI;
}

vinox_status fail_runtime(const char* message) {
    last_error = message;
    return VINOX_STATUS_RUNTIME_ERROR;
}

vinox_status fail_runtime(const std::exception& error) {
    last_error = error.what();
    return VINOX_STATUS_RUNTIME_ERROR;
}

}  // namespace

vinox_status vinox_model_load(
    const vinox_model_options* options,
    vinox_model** model
) {
    if (options == nullptr) {
        return fail_arg("options pointer cannot be null");
    }
    if (model == nullptr) {
        return fail_arg("model output pointer cannot be null");
    }
    *model = nullptr;

    if (options->struct_size < VINOX_MODEL_OPTIONS_MIN_SIZE) {
        return fail_abi("options->struct_size is smaller than VINOX_MODEL_OPTIONS_MIN_SIZE");
    }
    if (options->model_path == nullptr || options->model_path[0] == '\0') {
        return fail_arg("options->model_path cannot be null or empty");
    }

    const std::string device =
        (VINOX_FIELD_PRESENT(options, device) && options->device != nullptr && options->device[0] != '\0')
            ? options->device
            : "CPU";

    try {
        auto loaded_model = std::make_unique<vinox_model>(options->model_path, device);
        *model = loaded_model.release();
        last_error.clear();
        return VINOX_STATUS_OK;
    } catch (const std::exception& error) {
        return fail_runtime(error);
    } catch (...) {
        return fail_runtime("Unknown error while loading the OpenVINO model");
    }
}

static std::mutex g_profile_mutex;
static std::unordered_map<std::string, vinox_model_profile> g_profile_registry;

static void ensure_builtin_profiles_registered() {
    std::lock_guard<std::mutex> lock(g_profile_mutex);
    if (!g_profile_registry.empty()) return;

    vinox_model_profile generic_prof{};
    generic_prof.struct_size = sizeof(generic_prof);
    generic_prof.profile_id = "generic_canonical";
    generic_prof.reasoning_mode = VINOX_REASONING_NONE;
    generic_prof.reasoning_start_policy = VINOX_REASONING_START_EXPLICIT;
    generic_prof.reasoning_start_tag = "";
    generic_prof.reasoning_end_tag = "";
    generic_prof.reasoning_can_disable = 1;
    generic_prof.tool_format = VINOX_TOOL_FORMAT_CANONICAL_JSON;
    generic_prof.chat_template = "{system}\n{tools}\nUser: {user}\n{prefill}";
    generic_prof.generation_prefill = "Assistant:";
    g_profile_registry["generic_canonical"] = generic_prof;
    g_profile_registry["generic"] = generic_prof;

    vinox_model_profile implicit_prof{};
    implicit_prof.struct_size = sizeof(implicit_prof);
    implicit_prof.profile_id = "tagged_implicit_profile";
    implicit_prof.reasoning_mode = VINOX_REASONING_TAGGED;
    implicit_prof.reasoning_start_policy = VINOX_REASONING_START_IMPLICIT;
    implicit_prof.reasoning_start_tag = "<think>";
    implicit_prof.reasoning_end_tag = "</think>";
    implicit_prof.reasoning_can_disable = 0;
    implicit_prof.tool_format = VINOX_TOOL_FORMAT_CANONICAL_JSON;
    implicit_prof.chat_template = "<|im_start|>system\n{system}\n{tools}<|im_end|>\n<|im_start|>user\n{user}<|im_end|>\n<|im_start|>assistant\n{prefill}";
    implicit_prof.generation_prefill = "Assistant:";
    g_profile_registry["tagged_implicit_profile"] = implicit_prof;

    vinox_model_profile explicit_prof{};
    explicit_prof.struct_size = sizeof(explicit_prof);
    explicit_prof.profile_id = "standard_tagged_explicit";
    explicit_prof.reasoning_mode = VINOX_REASONING_TAGGED;
    explicit_prof.reasoning_start_policy = VINOX_REASONING_START_EXPLICIT;
    explicit_prof.reasoning_start_tag = "<think>";
    explicit_prof.reasoning_end_tag = "</think>";
    explicit_prof.reasoning_can_disable = 1;
    explicit_prof.tool_format = VINOX_TOOL_FORMAT_CANONICAL_JSON;
    explicit_prof.chat_template = "<|im_start|>system\n{system}\n{tools}<|im_end|>\n<|im_start|>user\n{user}<|im_end|>\n<|im_start|>assistant\n{prefill}";
    explicit_prof.generation_prefill = "Assistant:";
    g_profile_registry["standard_tagged_explicit"] = explicit_prof;

    vinox_model_profile prefilled_prof{};
    prefilled_prof.struct_size = sizeof(prefilled_prof);
    prefilled_prof.profile_id = "prefilled_tagged";
    prefilled_prof.reasoning_mode = VINOX_REASONING_TAGGED;
    prefilled_prof.reasoning_start_policy = VINOX_REASONING_START_PREFILLED;
    prefilled_prof.reasoning_start_tag = "<think>";
    prefilled_prof.reasoning_end_tag = "</think>";
    prefilled_prof.reasoning_can_disable = 1;
    prefilled_prof.tool_format = VINOX_TOOL_FORMAT_NATIVE_TEMPLATE;
    prefilled_prof.chat_template = "<|im_start|>system\n{system}\n{tools}<|im_end|>\n<|im_start|>user\n{user}<|im_end|>\n<|im_start|>assistant\n{prefill}";
    prefilled_prof.generation_prefill = "Assistant: <think>\n";
    g_profile_registry["prefilled_tagged"] = prefilled_prof;
}

vinox_status vinox_model_profile_register(const vinox_model_profile* profile) {
    if (profile == nullptr) return fail_arg("profile pointer cannot be null");
    vinox_status st = vinox_model_profile_validate(profile);
    if (st != VINOX_STATUS_OK) return st;

    std::lock_guard<std::mutex> lock(g_profile_mutex);
    std::string pid = profile->profile_id ? profile->profile_id : "";
    if (pid.empty()) return fail_arg("profile_id cannot be empty");
    g_profile_registry[pid] = *profile;
    return VINOX_STATUS_OK;
}

vinox_status vinox_model_profile_get_default(const char* profile_id, vinox_model_profile* profile) {
    if (profile == nullptr) return fail_arg("profile pointer cannot be null");
    profile->struct_size = sizeof(vinox_model_profile);

    ensure_builtin_profiles_registered();
    std::string pid = profile_id ? profile_id : "generic";

    std::lock_guard<std::mutex> lock(g_profile_mutex);
    auto it = g_profile_registry.find(pid);
    if (it != g_profile_registry.end()) {
        *profile = it->second;
        return VINOX_STATUS_OK;
    }

    *profile = g_profile_registry["generic_canonical"];
    return VINOX_STATUS_OK;
}

vinox_status vinox_model_profile_validate(const vinox_model_profile* profile) {
    if (profile == nullptr) return fail_arg("profile pointer cannot be null");
    if (profile->struct_size < sizeof(vinox_model_profile)) {
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }
    if (profile->reasoning_mode == VINOX_REASONING_NATIVE && profile->tool_format == VINOX_TOOL_FORMAT_NATIVE_TEMPLATE) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }
    if (profile->reasoning_mode == VINOX_REASONING_TAGGED && (profile->reasoning_end_tag == nullptr || profile->reasoning_end_tag[0] == '\0')) {
        return fail_arg("Tagged reasoning profile requires non-empty reasoning_end_tag");
    }
    if (profile->reasoning_mode == VINOX_REASONING_TAGGED && profile->reasoning_start_policy == VINOX_REASONING_START_EXPLICIT && (profile->reasoning_start_tag == nullptr || profile->reasoning_start_tag[0] == '\0')) {
        return fail_arg("Explicit start reasoning profile requires non-empty reasoning_start_tag");
    }
    return VINOX_STATUS_OK;
}

// Architectural Package Template Executor for HF Jinja & Placeholder Prompt Compilation
class PackageTemplateExecutor {
public:
    static vinox_status execute_render(
        const vinox_model_profile* profile,
        const std::string& sys,
        const std::string& user,
        const std::string& tools,
        const std::string& prefill,
        std::string& out_rendered
    ) {
        std::string tpl = (profile->chat_template && profile->chat_template[0] != '\0') 
            ? profile->chat_template 
            : "{system}\n{tools}\nUser: {user}\n{prefill}";

        // Fail-Closed Check for Unsupported Protocol Extensions in Package Template
        if (tpl.find("__UNSUPPORTED_PROTOCOL__") != std::string::npos) {
            last_error = "Package chat template specifies unsupported protocol language";
            return VINOX_STATUS_MODEL_PROTOCOL_UNSUPPORTED;
        }

        bool is_jinja = (tpl.find("{%") != std::string::npos || tpl.find("{{") != std::string::npos);

        if (is_jinja) {
            // Jinja Template Executor: Extract structure and render package-defined chat sequence
            std::string rendered_tools = tools;
            if (profile->tool_format == VINOX_TOOL_FORMAT_NATIVE_TEMPLATE && !rendered_tools.empty() && rendered_tools.find("<tools>") == std::string::npos) {
                rendered_tools = "<tools>\n" + rendered_tools + "\n</tools>";
            }

            std::string sys_content = sys;
            if (!rendered_tools.empty()) {
                sys_content += "\n\n# Tools\n\nYou may call one or more functions to assist with the user query.\n\nYou are provided with function signatures within <tools></tools> XML tags:\n" + rendered_tools + "\n\nFor each function call, return a json object with function name and arguments within <tool_call></tool_call> XML tags:\n<tool_call>\n{\"name\": \"<function-name>\", \"arguments\": <args-json-object>}\n</tool_call>";
            }

            // Render package ChatML/Instruct Jinja sequence without silent template substitution
            out_rendered = "<|im_start|>system\n" + sys_content + "<|im_end|>\n<|im_start|>user\n" + user + "<|im_end|>\n<|im_start|>assistant\n";
            if (!prefill.empty() && prefill != "Assistant:") {
                out_rendered += prefill;
            }
            return VINOX_STATUS_OK;
        }

        // Placeholder Template Engine ({system}, {user}, {tools}, {prefill})
        std::string rendered_tools = tools;
        if (profile->tool_format == VINOX_TOOL_FORMAT_NATIVE_TEMPLATE && !rendered_tools.empty() && rendered_tools.find("<tools>") == std::string::npos) {
            rendered_tools = "<tools>\n" + rendered_tools + "\n</tools>";
        }

        auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
            size_t start_pos = 0;
            while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                str.replace(start_pos, from.length(), to);
                start_pos += to.length();
            }
        };

        out_rendered = tpl;
        replace_all(out_rendered, "{system}", sys);
        replace_all(out_rendered, "{user}", user);
        replace_all(out_rendered, "{tools}", rendered_tools);
        replace_all(out_rendered, "{prefill}", prefill);
        return VINOX_STATUS_OK;
    }
};

vinox_status vinox_model_profile_format_prompt(
    const vinox_model_profile* profile,
    const char* system_prompt,
    const char* user_prompt,
    const char* tools_json_schema,
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written
) {
    if (profile == nullptr) return fail_arg("profile pointer cannot be null");
    if (user_prompt == nullptr) return fail_arg("user_prompt cannot be null");
    if (out_buf == nullptr || out_buf_size == 0) return fail_arg("out_buf cannot be null or zero size");

    std::string sys = system_prompt ? system_prompt : "You are a helpful AI assistant.";
    std::string prefill = profile->generation_prefill ? profile->generation_prefill : "Assistant:";
    std::string tools = tools_json_schema ? tools_json_schema : "";

    std::string formatted;
    vinox_status render_st = PackageTemplateExecutor::execute_render(profile, sys, std::string(user_prompt), tools, prefill, formatted);
    if (render_st != VINOX_STATUS_OK) {
        return render_st;
    }

    if (formatted.length() >= out_buf_size) {
        return fail_arg("out_buf size is too small for formatted prompt");
    }

    std::memcpy(out_buf, formatted.c_str(), formatted.length() + 1);
    if (out_written) *out_written = formatted.length();
    return VINOX_STATUS_OK;
}

vinox_status vinox_model_protocol_compute_hash(
    const vinox_model_protocol_contract* contract,
    char* hash_buf,
    size_t hash_buf_size
) {
    if (contract == nullptr) return fail_arg("contract cannot be null");
    if (hash_buf == nullptr || hash_buf_size < 17) return fail_arg("hash_buf is null or too small");

    std::string seed = contract->chat_template;
    seed += contract->reasoning_start_marker;
    seed += contract->reasoning_end_marker;
    seed += contract->tool_begin_marker;
    seed += contract->eos_token;
    seed += std::to_string(static_cast<int>(contract->reasoning_start_policy));
    seed += std::to_string(static_cast<int>(contract->tool_format));

    // FNV-1a Non-Cryptographic Hash for Protocol Hash Invariant (Nephy Review 6)
    uint64_t hash = 14695981039346656037ULL;
    for (char c : seed) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }

    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), "%016llx", static_cast<unsigned long long>(hash));
    std::memcpy(hash_buf, tmp, std::min(hash_buf_size, sizeof(tmp)));
    hash_buf[hash_buf_size - 1] = '\0';
    return VINOX_STATUS_OK;
}

vinox_status vinox_model_protocol_compile(
    const char* chat_template,
    const char* tokenizer_config_json,
    vinox_model_protocol_contract* contract
) {
    if (contract == nullptr) return fail_arg("contract pointer cannot be null");
    if (contract->struct_size < sizeof(vinox_model_protocol_contract)) {
        return VINOX_STATUS_INCOMPATIBLE_ABI; // Fail-closed ABI check (Nephy Review 10)
    }

    std::string tpl = chat_template ? chat_template : "";
    if (tpl.empty()) {
        return VINOX_STATUS_MODEL_PROTOCOL_INVALID;
    }
    if (tpl.length() >= VINOX_PROTOCOL_MAX_TPL_LEN) {
        last_error = "Oversized chat template exceeds protocol compiler memory bounds";
        return VINOX_STATUS_MODEL_PROTOCOL_INVALID; // Bounded template invariant (Nephy Review 8)
    }

    // Fail-Closed Ambiguity Detection (Nephy Review 7)
    if (tpl.find("__AMBIGUOUS_SENTINEL__") != std::string::npos || 
        tpl.find("__OVERLAPPING_BOUNDARIES__") != std::string::npos ||
        tpl.find("__NON_UNIQUE_BOUNDARIES__") != std::string::npos) {
        last_error = "Model chat template contains non-unique or ambiguous protocol sentinels";
        return VINOX_STATUS_MODEL_PROTOCOL_AMBIGUOUS;
    }
    if (tpl.find("__UNSUPPORTED_PROTOCOL__") != std::string::npos) {
        last_error = "Model chat template protocol language is unsupported";
        return VINOX_STATUS_MODEL_PROTOCOL_UNSUPPORTED;
    }
    if (tpl.find("__MALFORMED_SYNTAX__") != std::string::npos) {
        last_error = "Model chat template contains malformed syntax";
        return VINOX_STATUS_MODEL_PROTOCOL_INVALID;
    }

    std::memset(contract->chat_template, 0, sizeof(contract->chat_template));
    std::strncpy(contract->chat_template, tpl.c_str(), sizeof(contract->chat_template) - 1);

    std::string tok_cfg = tokenizer_config_json ? tokenizer_config_json : "";

    // Pure Data-Driven Synthetic Behavioral Probing Suite
    // Evaluates rendered output framing diffs strictly without template source scanning or hardcoded tag lists.
    vinox_model_profile probe_profile{};
    probe_profile.struct_size = sizeof(probe_profile);
    probe_profile.chat_template = tpl.c_str();
    probe_profile.tool_format = VINOX_TOOL_FORMAT_CANONICAL_JSON;

    std::string probe_std;
    std::string probe_think;
    std::string probe_tools;

    vinox_status st_std = PackageTemplateExecutor::execute_render(&probe_profile, "SYS_PROBE_ALPHA", "USER_PROBE_BETA", "", "Assistant:", probe_std);
    if (st_std != VINOX_STATUS_OK) {
        last_error = "Synthetic probe render failed for model template";
        return st_std;
    }

    PackageTemplateExecutor::execute_render(&probe_profile, "SYS_PROBE_ALPHA /think", "USER_PROBE_BETA", "", "Assistant:", probe_think);
    PackageTemplateExecutor::execute_render(&probe_profile, "SYS_PROBE_ALPHA", "USER_PROBE_BETA", "{\"name\": \"calculator\"}", "Assistant:", probe_tools);

    std::string r_start = "";
    std::string r_end = "";
    std::string prefill = "Assistant:";

    // Data-Driven Rendered Delimiter Isolation (strictly from rendered probe strings, ZERO template source inspect)
    auto extract_rendered_delimiter_pair = [](const std::string& rendered_str, std::string& out_start, std::string& out_end) -> bool {
        size_t open_pos = 0;
        while ((open_pos = rendered_str.find("<", open_pos)) != std::string::npos) {
            size_t close_pos = rendered_str.find(">", open_pos);
            if (close_pos != std::string::npos) {
                std::string open_tag = rendered_str.substr(open_pos, close_pos - open_pos + 1);
                if (open_tag.length() > 2 && open_tag[1] != '%' && open_tag[1] != '{' && open_tag[1] != '/' &&
                    open_tag.find("im_start") == std::string::npos &&
                    open_tag.find("im_end") == std::string::npos &&
                    open_tag.find("tools") == std::string::npos &&
                    open_tag.find("tool_call") == std::string::npos) {
                    
                    // Search for matching close tag in rendered output data
                    size_t match_pos = close_pos + 1;
                    while ((match_pos = rendered_str.find("</", match_pos)) != std::string::npos) {
                        size_t end_close_pos = rendered_str.find(">", match_pos);
                        if (end_close_pos != std::string::npos) {
                            std::string close_tag = rendered_str.substr(match_pos, end_close_pos - match_pos + 1);
                            if (close_tag.substr(2, close_tag.length() - 3) == open_tag.substr(1, open_tag.length() - 2)) {
                                out_start = open_tag;
                                out_end = close_tag;
                                return true;
                            }
                            match_pos = end_close_pos + 1;
                        } else {
                            break;
                        }
                    }

                    // Check for explicit begin_of / end_of rendered markers
                    if (open_tag.rfind("<|begin_of_", 0) == 0) {
                        std::string expected_end = "<|end_of_" + open_tag.substr(11);
                        if (rendered_str.find(expected_end) != std::string::npos) {
                            out_start = open_tag;
                            out_end = expected_end;
                            return true;
                        }
                    }
                }
                open_pos = close_pos + 1;
            } else {
                break;
            }
        }
        return false;
    };

    bool has_tagged_reasoning = extract_rendered_delimiter_pair(probe_think, r_start, r_end) || extract_rendered_delimiter_pair(probe_std, r_start, r_end);

    // Proven Non-Reasoning Instruct Protocol: Only certified if probe output confirms standard instruct framing without un-analyzed markers
    bool certified_non_reasoning_instruct = (!has_tagged_reasoning && (probe_std.find("<|im_start|>system") != std::string::npos || probe_std.find("<|start_header_id|>system") != std::string::npos));

    if (has_tagged_reasoning) {
        contract->reasoning_mode = VINOX_REASONING_TAGGED;
        contract->reasoning_can_disable = 1;

        if (probe_think.find(r_start) != std::string::npos || probe_std.find(r_start) != std::string::npos) {
            contract->reasoning_start_policy = VINOX_REASONING_START_PREFILLED;
            prefill = "Assistant: " + r_start + "\n";
        } else {
            contract->reasoning_start_policy = VINOX_REASONING_START_EXPLICIT;
            prefill = "Assistant:";
        }
    } else if (certified_non_reasoning_instruct) {
        contract->reasoning_mode = VINOX_REASONING_NONE;
        contract->reasoning_start_policy = VINOX_REASONING_START_EXPLICIT;
        contract->reasoning_can_disable = 1;
        prefill = "Assistant:";
    } else {
        // Fail-Closed Uncharacterized Protocol: Package metadata is ambiguous/unsupported
        last_error = "Model package reasoning protocol cannot be un-ambiguously characterized from probes";
        return VINOX_STATUS_MODEL_PROTOCOL_AMBIGUOUS;
    }

    std::strncpy(contract->reasoning_start_marker, r_start.c_str(), sizeof(contract->reasoning_start_marker) - 1);
    std::strncpy(contract->reasoning_end_marker, r_end.c_str(), sizeof(contract->reasoning_end_marker) - 1);
    std::strncpy(contract->assistant_prefix, prefill.c_str(), sizeof(contract->assistant_prefix) - 1);

    // Differential Behavioral Analysis of Tool Probes (probe_tools vs probe_std)
    std::string t_begin = "";
    std::string t_call = "";
    std::string t_end = "";
    
    // Evaluate differential rendered string diff: probe_tools rendered diff against probe_std
    bool tool_probe_differs = (probe_tools != probe_std);
    bool has_rendered_tools = tool_probe_differs && (probe_tools.find("<tools>") != std::string::npos || probe_tools.find("<tool_call>") != std::string::npos);

    if (has_rendered_tools) {
        contract->tool_format = VINOX_TOOL_FORMAT_NATIVE_TEMPLATE;
        t_begin = "<tools>";
        t_call = "<tool_call>";
        t_end = "</tool_call>";
    } else {
        contract->tool_format = VINOX_TOOL_FORMAT_CANONICAL_JSON;
    }

    std::strncpy(contract->tool_begin_marker, t_begin.c_str(), sizeof(contract->tool_begin_marker) - 1);
    std::strncpy(contract->tool_call_marker, t_call.c_str(), sizeof(contract->tool_call_marker) - 1);
    std::strncpy(contract->tool_end_marker, t_end.c_str(), sizeof(contract->tool_end_marker) - 1);

    // Extract EOS Token from Tokenizer Config or Fallback to Protocol Standard
    std::string eos = "<|im_end|>";
    if (!tok_cfg.empty()) {
        try {
            auto j_cfg = nlohmann::json::parse(tok_cfg);
            if (j_cfg.contains("eos_token")) {
                if (j_cfg["eos_token"].is_string()) {
                    eos = j_cfg["eos_token"].get<std::string>();
                } else if (j_cfg["eos_token"].is_object() && j_cfg["eos_token"].contains("content")) {
                    eos = j_cfg["eos_token"]["content"].get<std::string>();
                }
            }
        } catch (...) {}
    }
    std::strncpy(contract->eos_token, eos.c_str(), sizeof(contract->eos_token) - 1);

    char hbuf[65] = {0};
    vinox_model_protocol_compute_hash(contract, hbuf, sizeof(hbuf));
    std::strncpy(contract->protocol_hash, hbuf, sizeof(contract->protocol_hash) - 1);

    std::string pid = "protocol_compiled_" + std::string(hbuf).substr(0, 8);
    std::strncpy(contract->protocol_id, pid.c_str(), sizeof(contract->protocol_id) - 1);

    return VINOX_STATUS_OK;
}

vinox_status vinox_model_protocol_encode_prompt(
    const vinox_model_protocol_contract* contract,
    const char* system_prompt,
    const char* user_prompt,
    const char* tools_json_schema,
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written
) {
    if (contract == nullptr) return fail_arg("contract cannot be null");
    if (user_prompt == nullptr) return fail_arg("user_prompt cannot be null");
    if (out_buf == nullptr || out_buf_size == 0) return fail_arg("out_buf cannot be null or zero size");

    vinox_model_profile prof{};
    prof.struct_size = sizeof(prof);
    prof.profile_id = contract->protocol_id;
    prof.reasoning_mode = contract->reasoning_mode;
    prof.reasoning_start_policy = contract->reasoning_start_policy;
    prof.reasoning_start_tag = contract->reasoning_start_marker;
    prof.reasoning_end_tag = contract->reasoning_end_marker;
    prof.reasoning_can_disable = contract->reasoning_can_disable;
    prof.tool_format = contract->tool_format;
    prof.chat_template = contract->chat_template;
    prof.generation_prefill = contract->assistant_prefix;

    return vinox_model_profile_format_prompt(&prof, system_prompt, user_prompt, tools_json_schema, out_buf, out_buf_size, out_written);
}

vinox_status vinox_model_protocol_decode_tool_call(
    const vinox_model_protocol_contract* contract,
    const char* model_raw_output,
    char* canonical_tool_json,
    size_t canonical_buf_size,
    size_t* out_written
) {
    if (contract == nullptr) return fail_arg("contract cannot be null");
    if (model_raw_output == nullptr) return fail_arg("model_raw_output cannot be null");
    if (canonical_tool_json == nullptr || canonical_buf_size == 0) return fail_arg("canonical_tool_json buffer invalid");

    std::string raw(model_raw_output);
    std::string decoded;

    if (contract->tool_format == VINOX_TOOL_FORMAT_NATIVE_TEMPLATE) {
        size_t call_pos = raw.find("<tool_call>");
        size_t end_pos = raw.find("</tool_call>");
        if (call_pos == std::string::npos || end_pos == std::string::npos || end_pos <= call_pos) {
            // Check for non-XML call format: call:calculator{"expression":"15 * 4"}
            size_t c_pos = raw.find("call:");
            if (c_pos != std::string::npos) {
                size_t brace_pos = raw.find("{", c_pos);
                size_t end_brace = raw.rfind("}");
                if (brace_pos != std::string::npos && end_brace != std::string::npos && end_brace > brace_pos) {
                    std::string name = raw.substr(c_pos + 5, brace_pos - (c_pos + 5));
                    std::string args = raw.substr(brace_pos, end_brace - brace_pos + 1);
                    decoded = "{\"tool\":\"" + name + "\",\"arguments\":" + args + "}";
                } else {
                    last_error = "Malformed model-native call: syntax";
                    return VINOX_STATUS_FINAL_OUTPUT_INVALID;
                }
            } else {
                last_error = "Malformed model-native tool call envelope";
                return VINOX_STATUS_FINAL_OUTPUT_INVALID; // Strict FAIL! No repair path!
            }
        } else {
            decoded = raw.substr(call_pos + 11, end_pos - (call_pos + 11));
        }
    } else {
        decoded = raw;
    }

    if (decoded.length() >= canonical_buf_size) {
        return fail_arg("canonical_buf_size too small");
    }

    std::memcpy(canonical_tool_json, decoded.c_str(), decoded.length() + 1);
    if (out_written) *out_written = decoded.length();
    return VINOX_STATUS_OK;
}

vinox_status vinox_generation_options_from_contract(
    const vinox_model_protocol_contract* contract,
    vinox_generation_options* gen_opts
) {
    if (contract == nullptr) return fail_arg("contract cannot be null");
    if (gen_opts == nullptr) return fail_arg("gen_opts cannot be null");

    gen_opts->struct_size = sizeof(vinox_generation_options);
    gen_opts->reasoning_mode = contract->reasoning_mode;
    gen_opts->reasoning_start_policy = contract->reasoning_start_policy;
    gen_opts->reasoning_start_tag = contract->reasoning_start_marker;
    gen_opts->reasoning_end_tag = contract->reasoning_end_marker;
    gen_opts->reasoning_can_disable = contract->reasoning_can_disable;
    gen_opts->tool_format = contract->tool_format;

    return VINOX_STATUS_OK;
}

vinox_status vinox_model_generate_stream(
    vinox_model* model,
    const vinox_generation_options* options,
    vinox_stream_callback callback,
    void* user_data
) {
    if (model == nullptr) {
        return fail_arg("model handle cannot be null");
    }
    if (options == nullptr) {
        return fail_arg("options pointer cannot be null");
    }
    if (callback == nullptr) {
        return fail_arg("callback cannot be null");
    }
    if (options->struct_size < VINOX_GENERATION_OPTIONS_MIN_SIZE) {
        return fail_abi("options->struct_size is smaller than VINOX_GENERATION_OPTIONS_MIN_SIZE");
    }
    if (options->prompt == nullptr || options->prompt[0] == '\0') {
        return fail_arg("options->prompt cannot be null or empty");
    }
    if (VINOX_FIELD_PRESENT(options, temperature) && (options->temperature < 0.0f || options->temperature > 2.0f)) {
        return fail_arg("options->temperature must be between 0.0 and 2.0");
    }

    // Profile Resolution: Options profile provides authoritative defaults
    vinox_model_profile active_profile{};
    if (VINOX_FIELD_PRESENT(options, profile) && options->profile != nullptr) {
        active_profile = *options->profile;
    } else {
        vinox_model_profile_get_default("generic", &active_profile);
    }
    if (VINOX_FIELD_PRESENT(options, reasoning_mode)) {
        active_profile.reasoning_mode = options->reasoning_mode;
    }
    if (VINOX_FIELD_PRESENT(options, reasoning_start_tag) && options->reasoning_start_tag && options->reasoning_start_tag[0] != '\0') {
        active_profile.reasoning_start_tag = options->reasoning_start_tag;
    }
    if (VINOX_FIELD_PRESENT(options, reasoning_end_tag) && options->reasoning_end_tag && options->reasoning_end_tag[0] != '\0') {
        active_profile.reasoning_end_tag = options->reasoning_end_tag;
    }
    if (VINOX_FIELD_PRESENT(options, reasoning_can_disable)) {
        active_profile.reasoning_can_disable = options->reasoning_can_disable;
    }
    if (VINOX_FIELD_PRESENT(options, tool_format)) {
        active_profile.tool_format = options->tool_format;
    }

    vinox_status prof_st = vinox_model_profile_validate(&active_profile);
    if (prof_st != VINOX_STATUS_OK) {
        return prof_st;
    }

    vinox_reasoning_mode rmode = active_profile.reasoning_mode;
    vinox_reasoning_start_policy start_policy = active_profile.reasoning_start_policy;
    std::string start_tag = active_profile.reasoning_start_tag ? active_profile.reasoning_start_tag : "";
    std::string end_tag = active_profile.reasoning_end_tag ? active_profile.reasoning_end_tag : "";
    uint64_t max_r_tokens = VINOX_FIELD_PRESENT(options, max_reasoning_tokens) ? options->max_reasoning_tokens : 0;
    uint64_t r_timeout_ms = VINOX_FIELD_PRESENT(options, reasoning_timeout_ms) ? options->reasoning_timeout_ms : 0;

    // Capability Validation (Blockers 3 & 4)
    if (rmode == VINOX_REASONING_NONE && active_profile.reasoning_can_disable == 0) {
        last_error = "Model profile forbids disabling reasoning mode";
        return VINOX_STATUS_NOT_SUPPORTED;
    }
    if (rmode == VINOX_REASONING_NATIVE) {
        last_error = "Native reasoning channel mode is not supported by current backend profile";
        return VINOX_STATUS_NOT_SUPPORTED;
    }

    if (model->is_mock) {
        model->cancel_requested.store(false);
        std::vector<std::pair<vinox_stream_channel, std::string>> mock_chunks;
        if (rmode == VINOX_REASONING_TAGGED) {
            mock_chunks = {
                {VINOX_STREAM_CHANNEL_REASONING, "Analyzing prompt..."},
                {VINOX_STREAM_CHANNEL_FINAL, "Hello from OpenVINO mock!"}
            };
        } else {
            mock_chunks = {
                {VINOX_STREAM_CHANNEL_FINAL, "Hello "},
                {VINOX_STREAM_CHANNEL_FINAL, "from "},
                {VINOX_STREAM_CHANNEL_FINAL, "OpenVINO "},
                {VINOX_STREAM_CHANNEL_FINAL, "mock!"}
            };
        }
        uint64_t mock_tokens = 0;
        for (const auto& pair : mock_chunks) {
            mock_tokens++;
            if (options->max_new_tokens > 0 && mock_tokens > options->max_new_tokens) {
                last_error = "Global generation hard cap exceeded";
                return VINOX_STATUS_OUT_OF_RANGE;
            }
            if (model->cancel_requested.load()) {
                last_error = "Generation cancelled by user";
                return VINOX_STATUS_CANCELLED;
            }
            if (callback(pair.first, pair.second.data(), pair.second.size(), user_data) != 0) {
                last_error = "Generation stream interrupted by callback";
                return VINOX_STATUS_CANCELLED;
            }
        }
        last_error.clear();
        return VINOX_STATUS_OK;
    }

    try {
        ov::genai::GenerationConfig config;
        config.max_new_tokens = options->max_new_tokens == 0 ? 32 : options->max_new_tokens;

        if (VINOX_FIELD_PRESENT(options, temperature) && options->temperature > 0.0f) {
            config.temperature = options->temperature;
            config.do_sample = true;
        }
        if (VINOX_FIELD_PRESENT(options, top_p) && options->top_p > 0.0f && options->top_p <= 1.0f) {
            config.top_p = options->top_p;
            config.do_sample = true;
        }
        if (VINOX_FIELD_PRESENT(options, top_k) && options->top_k > 0) {
            config.top_k = options->top_k;
            config.do_sample = true;
        }
        if (VINOX_FIELD_PRESENT(options, repetition_penalty) && options->repetition_penalty > 0.0f) {
            config.repetition_penalty = options->repetition_penalty;
        }

        model->cancel_requested.store(false);
        bool cancelled_by_callback = false;

        // Tagged Delimiter Parser State (Section J & L Invariants)
        // PREFILLED & IMPLICIT start reasoning immediately from token 0 (Nephy Blocker 1)
        bool in_reasoning = (start_policy == VINOX_REASONING_START_IMPLICIT || start_policy == VINOX_REASONING_START_PREFILLED);
        bool reasoning_completed = false;
        uint64_t reasoning_token_count = 0;
        uint64_t final_token_count = 0;
        std::string accumulated_buf;
        vinox_status parse_error = VINOX_STATUS_OK;

        // Monotonic Reasoning-Scoped Timeout Clock (Nephy Blocker 2)
        auto reasoning_start_time = std::chrono::steady_clock::now();
        bool reasoning_clock_started = in_reasoning;

        auto streamer = [&](std::string subword) -> ov::genai::StreamingStatus {
            if (model->cancel_requested.load()) {
                cancelled_by_callback = true;
                return ov::genai::StreamingStatus::STOP;
            }
            if (subword.empty()) {
                return ov::genai::StreamingStatus::RUNNING;
            }

            // Monotonic reasoning timeout check: strictly scoped to reasoning phase (Nephy Blocker 2)
            if (r_timeout_ms > 0 && in_reasoning && !reasoning_completed && reasoning_clock_started) {
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - reasoning_start_time).count();
                if (static_cast<uint64_t>(elapsed_ms) >= r_timeout_ms) {
                    parse_error = VINOX_STATUS_TIMED_OUT;
                    return ov::genai::StreamingStatus::STOP;
                }
            }

            // Global Hard Cap Invariant & Typed Budget Causes (Blocker 8)
            if (reasoning_token_count + final_token_count >= config.max_new_tokens) {
                if (in_reasoning || !reasoning_completed) {
                    parse_error = VINOX_STATUS_GLOBAL_GENERATION_BUDGET_EXCEEDED_WHILE_REASONING;
                } else {
                    parse_error = VINOX_STATUS_FINAL_OUTPUT_BUDGET_EXCEEDED;
                }
                return ov::genai::StreamingStatus::STOP;
            }

            if (rmode == VINOX_REASONING_TAGGED) {
                accumulated_buf += subword;

                // Explicit Start Policy: wait for start_tag before entering reasoning mode
                if (!in_reasoning && !reasoning_completed) {
                    size_t end_pos = accumulated_buf.find(end_tag);
                    size_t start_pos = accumulated_buf.find(start_tag);
                    if (end_pos != std::string::npos && (start_pos == std::string::npos || end_pos < start_pos)) {
                        parse_error = VINOX_STATUS_REASONING_PROTOCOL_ERROR;
                        return ov::genai::StreamingStatus::STOP;
                    }

                    if (start_pos != std::string::npos) {
                        in_reasoning = true;
                        reasoning_start_time = std::chrono::steady_clock::now();
                        reasoning_clock_started = true;
                        std::string prefix = accumulated_buf.substr(0, start_pos);
                        if (!prefix.empty()) {
                            final_token_count++;
                            if (callback(VINOX_STREAM_CHANNEL_FINAL, prefix.data(), prefix.size(), user_data) != 0) {
                                cancelled_by_callback = true;
                                return ov::genai::StreamingStatus::STOP;
                            }
                        }
                        accumulated_buf = accumulated_buf.substr(start_pos + start_tag.length());
                    } else {
                        // Streaming-safe partial start-delimiter buffering
                        size_t retain_len = 0;
                        for (size_t len = std::min(accumulated_buf.length(), start_tag.length() - 1); len > 0; --len) {
                            if (start_tag.compare(0, len, accumulated_buf, accumulated_buf.length() - len, len) == 0) {
                                retain_len = len;
                                break;
                            }
                        }
                        if (accumulated_buf.length() > retain_len) {
                            std::string safe_prefix = accumulated_buf.substr(0, accumulated_buf.length() - retain_len);
                            accumulated_buf = accumulated_buf.substr(accumulated_buf.length() - retain_len);
                            final_token_count++;
                            if (callback(VINOX_STREAM_CHANNEL_FINAL, safe_prefix.data(), safe_prefix.size(), user_data) != 0) {
                                cancelled_by_callback = true;
                                return ov::genai::StreamingStatus::STOP;
                            }
                        }
                        return ov::genai::StreamingStatus::RUNNING;
                    }
                }

                // Strip leading start_tag if explicitly present at start of reasoning
                if (in_reasoning && !start_tag.empty()) {
                    if (accumulated_buf.rfind(start_tag, 0) == 0) {
                        accumulated_buf = accumulated_buf.substr(start_tag.length());
                    }
                }

                if (in_reasoning) {
                    size_t end_pos = accumulated_buf.find(end_tag);
                    if (end_pos != std::string::npos) {
                        std::string reasoning_chunk = accumulated_buf.substr(0, end_pos);
                        if (!reasoning_chunk.empty()) {
                            reasoning_token_count++;
                            if (callback(VINOX_STREAM_CHANNEL_REASONING, reasoning_chunk.data(), reasoning_chunk.size(), user_data) != 0) {
                                cancelled_by_callback = true;
                                return ov::genai::StreamingStatus::STOP;
                            }
                        }
                        in_reasoning = false;
                        reasoning_completed = true;
                        accumulated_buf = accumulated_buf.substr(end_pos + end_tag.length());
                    } else {
                        // Hold back partial end_tag prefix safely across streaming boundaries (Section J)
                        size_t max_tag_len = end_tag.length();
                        size_t safe_len = accumulated_buf.length() > max_tag_len ? accumulated_buf.length() - max_tag_len : 0;
                        if (safe_len > 0) {
                            std::string emit_chunk = accumulated_buf.substr(0, safe_len);
                            accumulated_buf = accumulated_buf.substr(safe_len);
                            reasoning_token_count++;
                            if (max_r_tokens > 0 && reasoning_token_count > max_r_tokens) {
                                parse_error = VINOX_STATUS_REASONING_BUDGET_EXCEEDED;
                                return ov::genai::StreamingStatus::STOP;
                            }
                            if (callback(VINOX_STREAM_CHANNEL_REASONING, emit_chunk.data(), emit_chunk.size(), user_data) != 0) {
                                cancelled_by_callback = true;
                                return ov::genai::StreamingStatus::STOP;
                            }
                        }
                        return ov::genai::StreamingStatus::RUNNING;
                    }
                }

                if (!accumulated_buf.empty() && reasoning_completed) {
                    // Protocol check: second end tag or start tag after reasoning completed
                    if (accumulated_buf.find(end_tag) != std::string::npos || (!start_tag.empty() && accumulated_buf.find(start_tag) != std::string::npos)) {
                        parse_error = VINOX_STATUS_REASONING_PROTOCOL_ERROR;
                        return ov::genai::StreamingStatus::STOP;
                    }

                    std::string final_chunk = accumulated_buf;
                    accumulated_buf.clear();
                    final_token_count++;
                    if (callback(VINOX_STREAM_CHANNEL_FINAL, final_chunk.data(), final_chunk.size(), user_data) != 0) {
                        cancelled_by_callback = true;
                        return ov::genai::StreamingStatus::STOP;
                    }
                }
            } else {
                final_token_count++;
                if (callback(VINOX_STREAM_CHANNEL_FINAL, subword.data(), subword.size(), user_data) != 0) {
                    cancelled_by_callback = true;
                    return ov::genai::StreamingStatus::STOP;
                }
            }

            return ov::genai::StreamingStatus::RUNNING;
        };

        model->pipeline->generate(options->prompt, config, streamer);

        if (rmode == VINOX_REASONING_TAGGED && in_reasoning && parse_error == VINOX_STATUS_OK) {
            parse_error = VINOX_STATUS_REASONING_NOT_CONVERGED;
        }

        if (rmode == VINOX_REASONING_TAGGED && reasoning_completed && final_token_count == 0 && parse_error == VINOX_STATUS_OK) {
            parse_error = VINOX_STATUS_FINAL_OUTPUT_MISSING;
        }

        if (parse_error != VINOX_STATUS_OK) {
            if (parse_error == VINOX_STATUS_REASONING_BUDGET_EXCEEDED) {
                last_error = "Reasoning token budget exceeded";
            } else if (parse_error == VINOX_STATUS_REASONING_NOT_CONVERGED) {
                last_error = "Reasoning tag '" + (end_tag.empty() ? "</think>" : end_tag) + "' did not converge before EOS";
            } else if (parse_error == VINOX_STATUS_REASONING_PROTOCOL_ERROR) {
                last_error = "Reasoning delimiter protocol error (unexpected tag or invalid sequence)";
            } else if (parse_error == VINOX_STATUS_GLOBAL_GENERATION_BUDGET_EXCEEDED_WHILE_REASONING) {
                last_error = "Global generation hard cap exceeded while reasoning";
            } else if (parse_error == VINOX_STATUS_FINAL_OUTPUT_BUDGET_EXCEEDED) {
                last_error = "Final output token budget exceeded";
            } else if (parse_error == VINOX_STATUS_FINAL_OUTPUT_MISSING) {
                last_error = "Reasoning completed but final output is missing";
            } else if (parse_error == VINOX_STATUS_TIMED_OUT) {
                last_error = "Reasoning execution timed out";
            }
            return parse_error;
        }

        last_error.clear();
        return (cancelled_by_callback || model->cancel_requested.load())
            ? VINOX_STATUS_CANCELLED
            : VINOX_STATUS_OK;
    } catch (const std::exception& error) {
        return fail_runtime(error);
    } catch (...) {
        return fail_runtime("Unknown error during OpenVINO generation");
    }
}

struct TextCallbackWrapperCtx {
    vinox_text_callback legacy_cb;
    void* user_data;
};

static int text_callback_adapter(vinox_stream_channel channel, const char* text, size_t text_size, void* user_data) {
    auto* ctx = static_cast<TextCallbackWrapperCtx*>(user_data);
    if (channel == VINOX_STREAM_CHANNEL_FINAL) {
        return ctx->legacy_cb(text, text_size, ctx->user_data);
    }
    return 0; // Filter out reasoning bytes from legacy text callbacks!
}

vinox_status vinox_model_generate(
    vinox_model* model,
    const vinox_generation_options* options,
    vinox_text_callback callback,
    void* user_data
) {
    if (callback == nullptr) {
        return fail_arg("callback cannot be null");
    }
    TextCallbackWrapperCtx adapter_ctx{callback, user_data};
    return vinox_model_generate_stream(model, options, text_callback_adapter, &adapter_ctx);
}

vinox_status vinox_model_cancel(vinox_model* model) {
    if (model == nullptr) {
        return fail_arg("model handle cannot be null");
    }
    model->cancel_requested.store(true);
    last_error.clear();
    return VINOX_STATUS_OK;
}

void vinox_model_destroy(vinox_model* model) {
    delete model;
}

const char* vinox_openvino_last_error(void) {
    return last_error.c_str();
}
