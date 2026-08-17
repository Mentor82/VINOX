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

    if (VINOX_FIELD_PRESENT(options, temperature) && options->temperature < 0.0f) {
        return fail_arg("Invalid generation option: temperature must be >= 0.0");
    }
    if (VINOX_FIELD_PRESENT(options, top_p) && (options->top_p < 0.0f || options->top_p > 1.0f)) {
        return fail_arg("Invalid generation option: top_p must be between 0.0 and 1.0");
    }
    if (VINOX_FIELD_PRESENT(options, repetition_penalty) && options->repetition_penalty < 0.0f) {
        return fail_arg("Invalid generation option: repetition_penalty must be >= 0.0");
    }

    vinox_reasoning_mode rmode = VINOX_REASONING_NONE;
    std::string start_tag = "<think>";
    std::string end_tag = "</think>";
    uint64_t max_r_tokens = 0;

    if (VINOX_FIELD_PRESENT(options, reasoning_mode)) {
        rmode = options->reasoning_mode;
    }
    if (VINOX_FIELD_PRESENT(options, reasoning_start_tag) && options->reasoning_start_tag && options->reasoning_start_tag[0] != '\0') {
        start_tag = options->reasoning_start_tag;
    }
    if (VINOX_FIELD_PRESENT(options, reasoning_end_tag) && options->reasoning_end_tag && options->reasoning_end_tag[0] != '\0') {
        end_tag = options->reasoning_end_tag;
    }
    if (VINOX_FIELD_PRESENT(options, max_reasoning_tokens)) {
        max_r_tokens = options->max_reasoning_tokens;
    }

    if (model->is_mock) {
        model->cancel_requested.store(false);
        std::vector<std::pair<vinox_stream_channel, std::string>> mock_chunks;
        if (rmode == VINOX_REASONING_TAGGED || rmode == VINOX_REASONING_NATIVE) {
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
        bool in_reasoning = false;
        bool reasoning_completed = false;
        uint64_t reasoning_token_count = 0;
        uint64_t final_token_count = 0;
        std::string accumulated_buf;
        vinox_status parse_error = VINOX_STATUS_OK;

        auto streamer = [&](std::string subword) -> ov::genai::StreamingStatus {
            if (model->cancel_requested.load()) {
                cancelled_by_callback = true;
                return ov::genai::StreamingStatus::STOP;
            }
            if (subword.empty()) {
                return ov::genai::StreamingStatus::RUNNING;
            }

            // Global Hard Cap Invariant (Section L)
            if (reasoning_token_count + final_token_count >= config.max_new_tokens) {
                parse_error = VINOX_STATUS_OUT_OF_RANGE;
                return ov::genai::StreamingStatus::STOP;
            }

            if (rmode == VINOX_REASONING_TAGGED) {
                accumulated_buf += subword;

                // Protocol check: unexpected end tag before reasoning started
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
                    }
                }

                if (in_reasoning) {
                    // Protocol check: duplicate start tag inside reasoning block
                    size_t dup_start = accumulated_buf.find(start_tag);
                    size_t end_pos = accumulated_buf.find(end_tag);
                    if (dup_start != std::string::npos && (end_pos == std::string::npos || dup_start < end_pos)) {
                        parse_error = VINOX_STATUS_REASONING_PROTOCOL_ERROR;
                        return ov::genai::StreamingStatus::STOP;
                    }

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

                if (!accumulated_buf.empty() && (!in_reasoning || reasoning_completed)) {
                    // Protocol check: second end tag or start tag after reasoning completed
                    if (reasoning_completed && (accumulated_buf.find(end_tag) != std::string::npos || accumulated_buf.find(start_tag) != std::string::npos)) {
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
                vinox_stream_channel ch = (rmode == VINOX_REASONING_NATIVE) ? VINOX_STREAM_CHANNEL_REASONING : VINOX_STREAM_CHANNEL_FINAL;
                if (ch == VINOX_STREAM_CHANNEL_REASONING) reasoning_token_count++; else final_token_count++;
                if (callback(ch, subword.data(), subword.size(), user_data) != 0) {
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

        if (parse_error != VINOX_STATUS_OK) {
            if (parse_error == VINOX_STATUS_REASONING_BUDGET_EXCEEDED) {
                last_error = "Reasoning token budget exceeded";
            } else if (parse_error == VINOX_STATUS_REASONING_NOT_CONVERGED) {
                last_error = "Reasoning tag </think> did not converge before EOS";
            } else if (parse_error == VINOX_STATUS_REASONING_PROTOCOL_ERROR) {
                last_error = "Reasoning delimiter protocol error (unexpected tag or invalid sequence)";
            } else if (parse_error == VINOX_STATUS_OUT_OF_RANGE) {
                last_error = "Global generation hard cap exceeded";
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
