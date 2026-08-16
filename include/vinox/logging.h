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

/**
 * @brief Emits a structured operational log event into active sinks.
 */
VINOX_API vinox_status vinox_log_event(
    uint32_t level,
    const char* component,
    const char* event_id,
    const vinox_correlation_context* correlation,
    const char* message_kv
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
 * @brief Configures minimum operational log level threshold.
 */
VINOX_API vinox_status vinox_log_set_level(uint32_t level);

/**
 * @brief Queries current minimum operational log level threshold.
 */
VINOX_API vinox_status vinox_log_get_level(uint32_t* level_out);

/**
 * @brief Configures rotating file log sink.
 */
VINOX_API vinox_status vinox_log_configure_sink(
    const char* log_file_path,
    uint32_t max_file_size_mb,
    uint32_t max_files
);

#ifdef __cplusplus
}
#endif

#endif /* VINOX_LOGGING_H */
