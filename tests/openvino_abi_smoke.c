#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vinox/openvino.h"

static int discard_text(const char* text, size_t text_size, void* user_data) {
    (void)text;
    (void)text_size;
    (void)user_data;
    return 0;
}

int main(void) {
    vinox_model_options model_options = {0};
    vinox_generation_options generation_options = {0};
    vinox_model* model = NULL;

    // 1. Null options pointer
    if (vinox_model_load(NULL, &model) != VINOX_STATUS_INVALID_ARGUMENT) {
        return 1;
    }
    if (vinox_openvino_last_error() == NULL || strlen(vinox_openvino_last_error()) == 0) {
        return 2;
    }

    // 2. Struct size too small for load (< VINOX_MODEL_OPTIONS_MIN_SIZE)
    model_options.struct_size = VINOX_MODEL_OPTIONS_MIN_SIZE - 1;
    if (vinox_model_load(&model_options, &model) != VINOX_STATUS_INCOMPATIBLE_ABI) {
        return 3;
    }
    if (strstr(vinox_openvino_last_error(), "smaller than") == NULL) {
        return 4;
    }

    // 3. Minimum supported vinox_model_options layout (up to model_path, without device)
    model_options.struct_size = VINOX_MODEL_OPTIONS_MIN_SIZE;
    model_options.model_path = NULL; // Invalid prompt/path test to check validation without loading
    if (vinox_model_load(&model_options, &model) != VINOX_STATUS_INVALID_ARGUMENT) {
        return 5;
    }
    if (strstr(vinox_openvino_last_error(), "model_path cannot be null") == NULL) {
        return 6;
    }

    // 4. Null model generate
    if (vinox_model_generate(NULL, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INVALID_ARGUMENT) {
        return 7;
    }

    // 5. Incompatible struct size for generate (size 0)
    generation_options.struct_size = 0;
    if (vinox_model_generate((vinox_model*)1, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INCOMPATIBLE_ABI) {
        return 8;
    }

    // 6. Minimum generation_options layout (up to max_new_tokens)
    generation_options.struct_size = VINOX_GENERATION_OPTIONS_MIN_SIZE;
    generation_options.prompt = NULL;
    if (vinox_model_generate((vinox_model*)1, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INVALID_ARGUMENT) {
        return 9;
    }
    if (strstr(vinox_openvino_last_error(), "prompt cannot be null") == NULL) {
        return 10;
    }

    // 7. Intermediate generation_options layout (up to top_p)
    generation_options.struct_size = (uint32_t)(offsetof(vinox_generation_options, top_p) + sizeof(float));
    generation_options.prompt = "Valid prompt";
    generation_options.temperature = -1.0f; // invalid temperature present in intermediate struct
    if (vinox_model_generate((vinox_model*)1, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INVALID_ARGUMENT) {
        return 11;
    }
    if (strstr(vinox_openvino_last_error(), "temperature") == NULL) {
        return 12;
    }

    // 8. Future tail-extended struct_size (e.g. +64 bytes tail padding) is accepted
    generation_options.struct_size = (uint32_t)(sizeof(vinox_generation_options) + 64);
    generation_options.prompt = "Valid prompt";
    generation_options.temperature = -1.0f;
    if (vinox_model_generate((vinox_model*)1, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INVALID_ARGUMENT) {
        return 13;
    }
    if (strstr(vinox_openvino_last_error(), "temperature") == NULL) {
        return 14;
    }

    // 9. Async cancel null check
    if (vinox_model_cancel(NULL) != VINOX_STATUS_INVALID_ARGUMENT) {
        return 15;
    }

    vinox_model_destroy(NULL);
    return 0;
}