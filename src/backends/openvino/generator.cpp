#include "vinox/openvino.h"

#include <atomic>
#include <exception>
#include <memory>
#include <string>
#include <type_traits>

#include "openvino/genai/llm_pipeline.hpp"

#include <vector>

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

vinox_status vinox_model_profile_get_default(const char* profile_id, vinox_model_profile* profile) {
    if (profile == nullptr) return fail_arg("profile pointer cannot be null");
    profile->struct_size = sizeof(vinox_model_profile);

    std::string pid = profile_id ? profile_id : "generic";
    if (pid == "deepseek_r1" || pid == "deepseek_r1_tagged_implicit") {
        profile->profile_id = "deepseek_r1_tagged_implicit";
        profile->reasoning_mode = VINOX_REASONING_TAGGED;
        profile->reasoning_start_policy = VINOX_REASONING_START_IMPLICIT;
        profile->reasoning_start_tag = "<think>";
        profile->reasoning_end_tag = "</think>";
        profile->reasoning_can_disable = 0;
        profile->tool_format = VINOX_TOOL_FORMAT_CANONICAL_JSON;
        profile->chat_template = "deepseek_r1_template";
        profile->generation_prefill = "Assistant:";
    } else if (pid == "qwen2_5" || pid == "standard_tagged_explicit") {
        profile->profile_id = "standard_tagged_explicit";
        profile->reasoning_mode = VINOX_REASONING_TAGGED;
        profile->reasoning_start_policy = VINOX_REASONING_START_EXPLICIT;
        profile->reasoning_start_tag = "<think>";
        profile->reasoning_end_tag = "</think>";
        profile->reasoning_can_disable = 1;
        profile->tool_format = VINOX_TOOL_FORMAT_CANONICAL_JSON;
        profile->chat_template = "standard_chat_template";
        profile->generation_prefill = "Assistant:";
    } else if (pid == "prefilled_tagged") {
        profile->profile_id = "prefilled_tagged";
        profile->reasoning_mode = VINOX_REASONING_TAGGED;
        profile->reasoning_start_policy = VINOX_REASONING_START_PREFILLED;
        profile->reasoning_start_tag = "<think>";
        profile->reasoning_end_tag = "</think>";
        profile->reasoning_can_disable = 1;
        profile->tool_format = VINOX_TOOL_FORMAT_CANONICAL_JSON;
        profile->chat_template = "prefilled_template";
        profile->generation_prefill = "Assistant: <think>\n";
    } else {
        profile->profile_id = "generic_canonical";
        profile->reasoning_mode = VINOX_REASONING_NONE;
        profile->reasoning_start_policy = VINOX_REASONING_START_EXPLICIT;
        profile->reasoning_start_tag = "";
        profile->reasoning_end_tag = "";
        profile->reasoning_can_disable = 1;
        profile->tool_format = VINOX_TOOL_FORMAT_CANONICAL_JSON;
        profile->chat_template = "generic_template";
        profile->generation_prefill = "Assistant:";
    }
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

    // Profile Resolution: Options profile provides authoritative defaults
    vinox_model_profile active_profile{};
    if (VINOX_FIELD_PRESENT(options, profile) && options->profile != nullptr) {
        active_profile = *options->profile;
    } else {
        vinox_model_profile_get_default("qwen2_5", &active_profile);
    }
    if (VINOX_FIELD_PRESENT(options, reasoning_mode)) {
        active_profile.reasoning_mode = options->reasoning_mode;
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
        bool in_reasoning = (start_policy == VINOX_REASONING_START_IMPLICIT);
        bool reasoning_completed = false;
        uint64_t reasoning_token_count = 0;
        uint64_t final_token_count = 0;
        std::string accumulated_buf;
        vinox_status parse_error = VINOX_STATUS_OK;
        auto gen_start_time = std::chrono::steady_clock::now();

        auto streamer = [&](std::string subword) -> ov::genai::StreamingStatus {
            if (model->cancel_requested.load()) {
                cancelled_by_callback = true;
                return ov::genai::StreamingStatus::STOP;
            }
            if (subword.empty()) {
                return ov::genai::StreamingStatus::RUNNING;
            }

            // Monotonic reasoning timeout check: strictly scoped to reasoning phase (Blocker 6)
            if (r_timeout_ms > 0 && !reasoning_completed) {
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gen_start_time).count();
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
                last_error = "Reasoning tag </think> did not converge before EOS";
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
