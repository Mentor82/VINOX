#include "vinox/openvino.h"

#include <exception>
#include <memory>
#include <string>

#include "openvino/genai/llm_pipeline.hpp"

struct vinox_model {
    explicit vinox_model(const std::string& model_path, const std::string& device)
        : pipeline(std::make_unique<ov::genai::LLMPipeline>(model_path, device)) {}

    std::unique_ptr<ov::genai::LLMPipeline> pipeline;
};

namespace {

thread_local std::string last_error;

vinox_status fail(const char* message) {
    last_error = message;
    return VINOX_STATUS_RUNTIME_ERROR;
}

vinox_status fail(const std::exception& error) {
    last_error = error.what();
    return VINOX_STATUS_RUNTIME_ERROR;
}

}  // namespace

vinox_status vinox_model_load(
    const vinox_model_options* options,
    vinox_model** model
) {
    if (options == nullptr || model == nullptr) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }
    *model = nullptr;
    if (options->struct_size != sizeof(vinox_model_options)) {
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }
    if (options->model_path == nullptr || options->model_path[0] == '\0') {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    const std::string device =
        options->device == nullptr || options->device[0] == '\0'
            ? "CPU"
            : options->device;

    try {
        auto loaded_model = std::make_unique<vinox_model>(options->model_path, device);
        *model = loaded_model.release();
        last_error.clear();
        return VINOX_STATUS_OK;
    } catch (const std::exception& error) {
        return fail(error);
    } catch (...) {
        return fail("Unknown error while loading the OpenVINO model");
    }
}

vinox_status vinox_model_generate(
    vinox_model* model,
    const vinox_generation_options* options,
    vinox_text_callback callback,
    void* user_data
) {
    if (model == nullptr || options == nullptr || callback == nullptr) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }
    if (options->struct_size != sizeof(vinox_generation_options)) {
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }
    if (options->prompt == nullptr || options->prompt[0] == '\0') {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    try {
        ov::genai::GenerationConfig config;
        config.max_new_tokens = options->max_new_tokens == 0
            ? 32
            : options->max_new_tokens;

        if (options->temperature > 0.0f) {
            config.temperature = options->temperature;
            config.do_sample = true;
        }
        if (options->top_p > 0.0f && options->top_p <= 1.0f) {
            config.top_p = options->top_p;
            config.do_sample = true;
        }
        if (options->top_k > 0) {
            config.top_k = options->top_k;
            config.do_sample = true;
        }
        if (options->repetition_penalty > 0.0f) {
            config.repetition_penalty = options->repetition_penalty;
        }
        if (options->presence_penalty != 0.0f) {
            config.presence_penalty = options->presence_penalty;
        }
        if (options->frequency_penalty != 0.0f) {
            config.frequency_penalty = options->frequency_penalty;
        }

        bool cancelled_by_callback = false;
        auto streamer = [callback, user_data, &cancelled_by_callback](std::string subword) -> ov::genai::StreamingStatus {
            if (subword.empty()) {
                return ov::genai::StreamingStatus::RUNNING;
            }
            if (callback(subword.data(), subword.size(), user_data) != 0) {
                cancelled_by_callback = true;
                return ov::genai::StreamingStatus::STOP;
            }
            return ov::genai::StreamingStatus::RUNNING;
        };

        model->pipeline->generate(options->prompt, config, streamer);
        last_error.clear();
        return cancelled_by_callback ? VINOX_STATUS_CANCELLED : VINOX_STATUS_OK;
    } catch (const std::exception& error) {
        return fail(error);
    } catch (...) {
        return fail("Unknown error during OpenVINO generation");
    }
}

void vinox_model_destroy(vinox_model* model) {
    delete model;
}

const char* vinox_openvino_last_error(void) {
    return last_error.c_str();
}
