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

vinox_status vinox_model_generate(
    vinox_model* model,
    const vinox_generation_options* options,
    vinox_text_callback callback,
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

    // Per-field presence validation
    if (VINOX_FIELD_PRESENT(options, temperature) && options->temperature < 0.0f) {
        return fail_arg("Invalid generation option: temperature must be >= 0.0");
    }
    if (VINOX_FIELD_PRESENT(options, top_p) && (options->top_p < 0.0f || options->top_p > 1.0f)) {
        return fail_arg("Invalid generation option: top_p must be between 0.0 and 1.0");
    }
    if (VINOX_FIELD_PRESENT(options, repetition_penalty) && options->repetition_penalty < 0.0f) {
        return fail_arg("Invalid generation option: repetition_penalty must be >= 0.0");
    }

    if (model->is_mock) {
        model->cancel_requested.store(false);
        std::vector<std::string> mock_chunks = {"Hello ", "from ", "OpenVINO ", "mock!"};
        for (const auto& chunk : mock_chunks) {
            if (model->cancel_requested.load()) {
                last_error = "Generation cancelled by user";
                return VINOX_STATUS_CANCELLED;
            }
            if (callback(chunk.data(), chunk.size(), user_data) != 0) {
                last_error = "Generation stream interrupted by callback";
                return VINOX_STATUS_CANCELLED;
            }
        }
        last_error.clear();
        return VINOX_STATUS_OK;
    }

    try {
        ov::genai::GenerationConfig config;
        config.max_new_tokens = options->max_new_tokens == 0
            ? 32
            : options->max_new_tokens;

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
        if (VINOX_FIELD_PRESENT(options, presence_penalty) && options->presence_penalty != 0.0f) {
            config.presence_penalty = options->presence_penalty;
        }
        if (VINOX_FIELD_PRESENT(options, frequency_penalty) && options->frequency_penalty != 0.0f) {
            config.frequency_penalty = options->frequency_penalty;
        }

        model->cancel_requested.store(false);
        bool cancelled_by_callback = false;

        auto streamer = [model, callback, user_data, &cancelled_by_callback](std::string subword) -> ov::genai::StreamingStatus {
            if (model->cancel_requested.load()) {
                cancelled_by_callback = true;
                return ov::genai::StreamingStatus::STOP;
            }
            if (subword.empty()) {
                return ov::genai::StreamingStatus::RUNNING;
            }
            if (callback(subword.data(), subword.size(), user_data) != 0) {
                cancelled_by_callback = true;
                return ov::genai::StreamingStatus::STOP;
            }
            if (model->cancel_requested.load()) {
                cancelled_by_callback = true;
                return ov::genai::StreamingStatus::STOP;
            }
            return ov::genai::StreamingStatus::RUNNING;
        };

        model->pipeline->generate(options->prompt, config, streamer);
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
