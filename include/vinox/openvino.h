#ifndef VINOX_OPENVINO_H
#define VINOX_OPENVINO_H

#include <stddef.h>
#include <stdint.h>

#include "vinox/export.h"
#include "vinox/vinox.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle representing a loaded VINOX OpenVINO model instance.
 *
 * @note Thread-Safety Contract:
 * - A single `vinox_model` handle is NOT thread-safe for concurrent calls to `vinox_model_generate`.
 * - Multi-threaded servers or clients must either pool `vinox_model` instances or serialize calls per handle.
 * - `vinox_model_cancel` is thread-safe and may be called asynchronously from any thread during generation.
 */
typedef struct vinox_model vinox_model;

typedef struct vinox_model_options {
    uint32_t struct_size;
    const char* model_path;
    const char* device;
} vinox_model_options;

#define VINOX_MODEL_OPTIONS_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_model_options, model_path) + sizeof(const char*)))

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

/* Minimum required struct_size for backward compatibility (up to max_new_tokens) */
#define VINOX_GENERATION_OPTIONS_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_generation_options, max_new_tokens) + sizeof(uint64_t)))

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

/**
 * @brief Asynchronously requests cancellation of an ongoing generation on `model`.
 *
 * Can be called safely from any thread while `vinox_model_generate` is running.
 */
VINOX_API vinox_status vinox_model_cancel(vinox_model* model);

VINOX_API void vinox_model_destroy(vinox_model* model);

/**
 * @brief Returns the last error message for the current thread.
 * Guaranteed to reflect the error reason for the last failed VINOX OpenVINO API call.
 */
VINOX_API const char* vinox_openvino_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
