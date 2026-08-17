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

typedef enum vinox_reasoning_mode {
    VINOX_REASONING_NONE = 0,
    VINOX_REASONING_TAGGED = 1,
    VINOX_REASONING_NATIVE = 2
} vinox_reasoning_mode;

typedef enum vinox_stream_channel {
    VINOX_STREAM_CHANNEL_FINAL = 0,
    VINOX_STREAM_CHANNEL_REASONING = 1
} vinox_stream_channel;

typedef enum vinox_reasoning_start_policy {
    VINOX_REASONING_START_EXPLICIT = 0,
    VINOX_REASONING_START_PREFILLED = 1,
    VINOX_REASONING_START_IMPLICIT = 2
} vinox_reasoning_start_policy;

typedef enum vinox_tool_format_mode {
    VINOX_TOOL_FORMAT_CANONICAL_JSON = 0,
    VINOX_TOOL_FORMAT_NATIVE_TEMPLATE = 1,
    VINOX_TOOL_FORMAT_NATIVE_CHANNEL = 2
} vinox_tool_format_mode;

typedef struct vinox_model_profile {
    uint32_t struct_size;
    const char* profile_id;
    vinox_reasoning_mode reasoning_mode;
    vinox_reasoning_start_policy reasoning_start_policy;
    const char* reasoning_start_tag;
    const char* reasoning_end_tag;
    int reasoning_can_disable;
    vinox_tool_format_mode tool_format;
    const char* chat_template;
    const char* generation_prefill;
} vinox_model_profile;

VINOX_API vinox_status vinox_model_profile_get_default(const char* profile_id, vinox_model_profile* profile);
VINOX_API vinox_status vinox_model_profile_validate(const vinox_model_profile* profile);

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
    vinox_reasoning_mode reasoning_mode;
    const char* reasoning_start_tag;
    const char* reasoning_end_tag;
    uint64_t max_reasoning_tokens;
    uint64_t reasoning_timeout_ms;
    int reasoning_can_disable; /* 1 = can disable reasoning, 0 = cannot disable reasoning */
    vinox_reasoning_start_policy reasoning_start_policy;
    vinox_tool_format_mode tool_format;
    const vinox_model_profile* profile;
} vinox_generation_options;

/* Minimum required struct_size for backward compatibility (up to max_new_tokens) */
#define VINOX_GENERATION_OPTIONS_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_generation_options, max_new_tokens) + sizeof(uint64_t)))

typedef int (*vinox_text_callback)(
    const char* text,
    size_t text_size,
    void* user_data
);

typedef int (*vinox_stream_callback)(
    vinox_stream_channel channel,
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

VINOX_API vinox_status vinox_model_generate_stream(
    vinox_model* model,
    const vinox_generation_options* options,
    vinox_stream_callback callback,
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
