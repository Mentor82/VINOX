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
    uint8_t enable_mmap;       /**< 1 = enable ov::enable_mmap (zero-copy virtual memory mapping), 0 = disable */
    uint8_t enable_cache;      /**< 1 = enable ov::cache_dir for compiled model blobs, 0 = disable */
    const char* cache_dir;     /**< Path to directory for cached model execution blobs (e.g. on NVMe) */
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

VINOX_API vinox_status vinox_model_profile_register(const vinox_model_profile* profile);
VINOX_API vinox_status vinox_model_profile_get_default(const char* profile_id, vinox_model_profile* profile);
VINOX_API vinox_status vinox_model_profile_validate(const vinox_model_profile* profile);
VINOX_API vinox_status vinox_model_profile_format_prompt(
    const vinox_model_profile* profile,
    const char* system_prompt,
    const char* user_prompt,
    const char* tools_json_schema,
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written
);

#define VINOX_PROTOCOL_MAX_STR_LEN 256
#define VINOX_PROTOCOL_MAX_TPL_LEN 16384

typedef struct vinox_model_protocol_contract {
    uint32_t struct_size;
    char protocol_id[64];
    char protocol_hash[65];
    vinox_reasoning_mode reasoning_mode;
    vinox_reasoning_start_policy reasoning_start_policy;
    char reasoning_start_marker[VINOX_PROTOCOL_MAX_STR_LEN];
    char reasoning_end_marker[VINOX_PROTOCOL_MAX_STR_LEN];
    int reasoning_can_disable;
    vinox_tool_format_mode tool_format;
    char tool_begin_marker[VINOX_PROTOCOL_MAX_STR_LEN];
    char tool_call_marker[VINOX_PROTOCOL_MAX_STR_LEN];
    char tool_end_marker[VINOX_PROTOCOL_MAX_STR_LEN];
    char assistant_prefix[VINOX_PROTOCOL_MAX_STR_LEN];
    char eos_token[VINOX_PROTOCOL_MAX_STR_LEN];
    char chat_template[VINOX_PROTOCOL_MAX_TPL_LEN];
} vinox_model_protocol_contract;

VINOX_API vinox_status vinox_model_protocol_compile(
    const char* chat_template,
    const char* tokenizer_config_json,
    vinox_model_protocol_contract* contract
);

VINOX_API vinox_status vinox_model_protocol_compute_hash(
    const vinox_model_protocol_contract* contract,
    char* hash_buf,
    size_t hash_buf_size
);

VINOX_API vinox_status vinox_model_protocol_encode_prompt(
    const vinox_model_protocol_contract* contract,
    const char* system_prompt,
    const char* user_prompt,
    const char* tools_json_schema,
    char* out_buf,
    size_t out_buf_size,
    size_t* out_written
);

VINOX_API vinox_status vinox_model_protocol_decode_tool_call(
    const vinox_model_protocol_contract* contract,
    const char* model_raw_output,
    char* canonical_tool_json,
    size_t canonical_buf_size,
    size_t* out_written
);

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
    const char* structured_output_json_schema; /* Optional JSONSchema for constrained decoding (OpenVINO GenAI 2026.3) */
    uint32_t enable_native_tool_parser;        /* 1 = attach OpenVINO GenAI 2026.3 native tool parsers */
} vinox_generation_options;

VINOX_API vinox_status vinox_generation_options_from_contract(
    const vinox_model_protocol_contract* contract,
    vinox_generation_options* gen_opts
);

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

/**
 * @brief Information about an OpenVINO execution device.
 */
typedef struct vinox_device_info {
    uint32_t struct_size;
    char device_id[32];      /**< e.g. "NPU", "GPU", "CPU" */
    char full_name[128];     /**< Full hardware name e.g. "Intel(R) AI Boost" */
    uint32_t priority;       /**< 1 = highest (NPU), 2 = GPU, 3 = CPU */
    uint8_t is_available;
} vinox_device_info;

/**
 * @brief Queries all available OpenVINO hardware devices, sorting them by strict execution priority (NPU > GPU > CPU).
 */
VINOX_API vinox_status vinox_devices_query(
    vinox_device_info* out_devices,
    size_t max_count,
    size_t* out_count,
    char* out_prioritized_device,
    size_t prioritized_device_size
);

/**
 * @brief Information about a storage device hosting a model or cache path.
 */
typedef struct vinox_storage_info {
    uint32_t struct_size;
    uint8_t is_nvme;
    uint8_t is_ssd;
    char bus_type_name[32];
    char device_name[128];
    uint64_t total_bytes;
    uint64_t free_bytes;
} vinox_storage_info;

/**
 * @brief Detects hardware storage properties (NVMe, SSD, Bus Type, Capacity) for a given path.
 */
VINOX_API vinox_status vinox_storage_detect(
    const char* path,
    vinox_storage_info* info
);

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
