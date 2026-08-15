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

    // 2. Struct size too small for load
    if (vinox_model_load(&model_options, &model) != VINOX_STATUS_INCOMPATIBLE_ABI) {
        return 3;
    }
    if (strstr(vinox_openvino_last_error(), "smaller than") == NULL) {
        return 4;
    }

    // 3. Null model generate
    if (vinox_model_generate(NULL, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INVALID_ARGUMENT) {
        return 5;
    }

    // 4. Incompatible struct size for generate (size 0)
    if (vinox_model_generate((vinox_model*)1, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INCOMPATIBLE_ABI) {
        return 6;
    }

    // 5. Flexible MIN_SIZE check: VINOX_GENERATION_OPTIONS_MIN_SIZE is accepted as ABI valid.
    // With NULL prompt, it fails with INVALID_ARGUMENT (not INCOMPATIBLE_ABI).
    generation_options.struct_size = VINOX_GENERATION_OPTIONS_MIN_SIZE;
    generation_options.prompt = NULL;
    if (vinox_model_generate((vinox_model*)1, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INVALID_ARGUMENT) {
        return 7;
    }
    if (strstr(vinox_openvino_last_error(), "prompt cannot be null") == NULL) {
        return 8;
    }

    // 6. Invalid sampling parameter (temperature < 0)
    generation_options.struct_size = (uint32_t)sizeof(generation_options);
    generation_options.prompt = "Valid prompt";
    generation_options.temperature = -1.0f;
    if (vinox_model_generate((vinox_model*)1, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INVALID_ARGUMENT) {
        return 9;
    }
    if (strstr(vinox_openvino_last_error(), "temperature") == NULL) {
        return 10;
    }

    // 7. Async cancel null check
    if (vinox_model_cancel(NULL) != VINOX_STATUS_INVALID_ARGUMENT) {
        return 11;
    }

    vinox_model_destroy(NULL);
    return 0;
}