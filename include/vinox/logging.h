#ifndef VINOX_LOGGING_H
#define VINOX_LOGGING_H

#include <stddef.h>
#include <stdint.h>

#include "vinox/export.h"
#include "vinox/vinox.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vinox_log_level {
    VINOX_LOG_TRACE    = 0,
    VINOX_LOG_DEBUG    = 1,
    VINOX_LOG_INFO     = 2,
    VINOX_LOG_WARN     = 3,
    VINOX_LOG_ERROR    = 4,
    VINOX_LOG_CRITICAL = 5
} vinox_log_level;

#define VINOX_CORRELATION_CONTEXT_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_correlation_context, request_id) + sizeof(const char*)))

typedef struct vinox_correlation_context {
    uint32_t struct_size;
    const char* request_id;
    const char* session_id;
    const char* run_id;
    const char* task_id;
    const char* operation_id;
} vinox_correlation_context;

#define VINOX_LOG_EVENT_META_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_log_event_meta, details) + sizeof(const char*)))

typedef struct vinox_log_event_meta {
    uint32_t struct_size;
    const char* model_id;
    const char* backend;
    uint64_t duration_ms;
    const char* status;
    uint32_t status_code;
    const char* details;
} vinox_log_event_meta;

/**
 * @brief Emits a structured operational log event into active sinks with JSON escaping and simple message details.
 */
VINOX_API vinox_status vinox_log_event(
    uint32_t level,
    const char* component,
    const char* event_id,
    const vinox_correlation_context* correlation,
    const char* message_kv
);

/**
 * @brief Emits a fully-typed canonical structured operational log event with top-level model_id, backend, duration_ms, status, status_code, and details.
 */
VINOX_API vinox_status vinox_log_event_ex(
    uint32_t level,
    const char* component,
    const char* event_id,
    const vinox_correlation_context* correlation,
    const vinox_log_event_meta* meta
);

/**
 * @brief Serializes a correlation context into a versioned process-boundary wire format JSON envelope.
 */
VINOX_API vinox_status vinox_correlation_serialize_envelope(
    const vinox_correlation_context* correlation,
    char* output_buf,
    size_t output_buf_size,
    size_t* required_size_out
);

/**
 * @brief Deserializes a versioned process-boundary wire format JSON envelope into a correlation context.
 */
VINOX_API vinox_status vinox_correlation_deserialize_envelope(
    const char* json_str,
    vinox_correlation_context* correlation_out,
    char* string_pool_buf,
    size_t string_pool_buf_size
);

/**
 * @brief Redacts sensitive API keys, bearer tokens, and secrets from input text.
 */
VINOX_API vinox_status vinox_redact_sensitive_text(
    const char* input_text,
    char* output_buf,
    size_t output_buf_size,
    size_t* required_size_out
);

/**
 * @brief Sets thread-local last_error with central secret redaction.
 */
VINOX_API void vinox_set_last_error(const char* message);

/**
 * @brief Retrieves thread-local redacted error message.
 */
VINOX_API const char* vinox_last_error(void);

/**
 * @brief Configures minimum operational log level threshold (must be <= VINOX_LOG_CRITICAL).
 */
VINOX_API vinox_status vinox_log_set_level(uint32_t level);

/**
 * @brief Queries current minimum operational log level threshold.
 */
VINOX_API vinox_status vinox_log_get_level(uint32_t* level_out);

/**
 * @brief Configures rotating file and console log sinks.
 */
VINOX_API vinox_status vinox_log_configure_sink(
    const char* log_file_path,
    uint32_t max_file_size_mb,
    uint32_t max_files
);

/**
 * @brief Queries observable sink health status without recursive log loops.
 */
VINOX_API vinox_status vinox_log_get_sink_status(
    uint32_t* sink_ok_out,
    uint64_t* dropped_count_out
);

#ifdef __cplusplus
}
#endif

#endif /* VINOX_LOGGING_H */
