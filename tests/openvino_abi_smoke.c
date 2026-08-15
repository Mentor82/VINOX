#include <stdint.h>

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

    if (vinox_model_load(NULL, &model) != VINOX_STATUS_INVALID_ARGUMENT) {
        return 1;
    }
    if (vinox_model_load(&model_options, &model) != VINOX_STATUS_INCOMPATIBLE_ABI) {
        return 2;
    }
    if (vinox_model_generate(NULL, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INVALID_ARGUMENT) {
        return 3;
    }
    if (vinox_model_generate((vinox_model*)1, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INCOMPATIBLE_ABI) {
        return 4;
    }

    generation_options.struct_size = (uint32_t)sizeof(generation_options);
    if (vinox_model_generate((vinox_model*)1, &generation_options, discard_text, NULL)
        != VINOX_STATUS_INVALID_ARGUMENT) {
        return 5;
    }

    if (vinox_openvino_last_error() == NULL) {
        return 6;
    }

    vinox_model_destroy(NULL);
    return 0;
}