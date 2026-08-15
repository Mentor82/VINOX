#ifndef VINOX_OPENVINO_H
#define VINOX_OPENVINO_H

#include <stddef.h>
#include <stdint.h>

#include "vinox/export.h"
#include "vinox/vinox.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vinox_model vinox_model;

typedef struct vinox_model_options {
    uint32_t struct_size;
    const char* model_path;
    const char* device;
} vinox_model_options;

typedef struct vinox_generation_options {
    uint32_t struct_size;
    const char* prompt;
    uint64_t max_new_tokens;
    float temperature;
    float top_p;
    size_t top_k;
    float repetition_penalty;
    float presence_penalty;
    float frequency_penalty;
} vinox_generation_options;

typedef int (*vinox_text_callback)(
    const char* text,
    size_t text_size,
    void* user_data
);

VINOX_API vinox_status vinox_model_load(
    const vinox_model_options* options,
    vinox_model** model
);

VINOX_API vinox_status vinox_model_generate(
    vinox_model* model,
    const vinox_generation_options* options,
    vinox_text_callback callback,
    void* user_data
);

VINOX_API void vinox_model_destroy(vinox_model* model);

VINOX_API const char* vinox_openvino_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
