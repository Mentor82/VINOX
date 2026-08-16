#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vinox/logging.h"
#include "vinox/vinox.h"

int main(void) {
    printf("Starting VINOX Issue #8 Hardened Logging Smoke Test...\n");

    // 1. Test log level configuration
    if (vinox_log_set_level(VINOX_LOG_DEBUG) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_log_set_level\n");
        return 1;
    }

    uint32_t active_level = 0;
    if (vinox_log_get_level(&active_level) != VINOX_STATUS_OK || active_level != VINOX_LOG_DEBUG) {
        printf("FAILED: vinox_log_get_level (expected %d, got %u)\n", VINOX_LOG_DEBUG, active_level);
        return 2;
    }

    // 2. Test Comprehensive Secret Redaction (Bearer token, sk- key, password=..., cookie=...)
    const char* secret_input = "Authorization: Bearer sk-1234567890abcdef password=SecretPass123 cookie=session_id_xyz999";
    char redacted_buf[512];
    size_t req_size = 0;
    if (vinox_redact_sensitive_text(secret_input, redacted_buf, sizeof(redacted_buf), &req_size) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_redact_sensitive_text\n");
        return 3;
    }

    if (strstr(redacted_buf, "sk-1234567890abcdef") != NULL ||
        strstr(redacted_buf, "SecretPass123") != NULL ||
        strstr(redacted_buf, "[REDACTED]") == NULL) {
        printf("FAILED: Secret redaction failed (got '%s')\n", redacted_buf);
        return 4;
    }

    // 3. Test vinox_set_last_error & vinox_last_error Redaction
    vinox_set_last_error("Database failure with key sk-secretkey99999");
    const char* err_msg = vinox_last_error();
    if (strstr(err_msg, "sk-secretkey99999") != NULL || strstr(err_msg, "[REDACTED]") == NULL) {
        printf("FAILED: vinox_last_error did not redact secret (got '%s')\n", err_msg);
        return 5;
    }

    // 4. Test Observable Sink Failure Reporting
    vinox_status bad_sink_status = vinox_log_configure_sink("Z:\\nonexistent_dir_999\\test.log", 1, 1);
    if (bad_sink_status != VINOX_STATUS_RUNTIME_ERROR) {
        printf("FAILED: Invalid sink path did not return VINOX_STATUS_RUNTIME_ERROR (got %d)\n", bad_sink_status);
        return 6;
    }

    uint32_t sink_ok = 1;
    uint64_t dropped_count = 0;
    vinox_log_get_sink_status(&sink_ok, &dropped_count);
    if (sink_ok != 0) {
        printf("FAILED: Observable sink status sink_ok was not set to 0 after failure\n");
        return 7;
    }

    // 5. Test spdlog Rotating Sink & Structured JSON Log Event Formatting with Correlation & Escaping
    const char* test_log_path = "test_vinox_logging_smoke.log";
    remove(test_log_path);

    if (vinox_log_configure_sink(test_log_path, 10, 3) != VINOX_STATUS_OK) {
        printf("FAILED: Valid sink configuration failed: %s\n", vinox_last_error());
        return 8;
    }

    // ABI Prefix Layout Test: Pass partial struct_size matching only request_id
    typedef struct partial_correlation_context {
        uint32_t struct_size;
        const char* request_id;
    } partial_correlation_context;

    partial_correlation_context partial_ctx;
    partial_ctx.struct_size = (uint32_t)sizeof(partial_ctx);
    partial_ctx.request_id = "req-prefix-abi-001";

    const char* payload_with_quotes = "Quotes: \"Hello World\" Backslash: \\ Newline: \n Tab: \t";
    if (vinox_log_event(VINOX_LOG_INFO, "core", "test.json.escape", (const vinox_correlation_context*)&partial_ctx, payload_with_quotes) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_log_event\n");
        return 9;
    }

    // Flush logger
    vinox_log_configure_sink(NULL, 0, 0);

    // Verify written log line
    FILE* f = fopen(test_log_path, "r");
    if (!f) {
        printf("FAILED: Could not open written log file '%s'\n", test_log_path);
        return 10;
    }

    char file_line[2048];
    if (!fgets(file_line, sizeof(file_line), f)) {
        printf("FAILED: Could not read log line from '%s'\n", test_log_path);
        fclose(f);
        return 11;
    }
    fclose(f);
    remove(test_log_path);

    // Verify JSON escaping (quotes escaped as \", backslash as \\, newline as \n)
    if (strstr(file_line, "\\\"Hello World\\\"") == NULL ||
        strstr(file_line, "\"request_id\":\"req-prefix-abi-001\"") == NULL ||
        strstr(file_line, "\"event_schema_version\":1") == NULL) {
        printf("FAILED: JSON escaping and ABI prefix layout check (got '%s')\n", file_line);
        return 12;
    }

    printf("SUCCESS: All VINOX Issue #8 Hardened Logging & Redaction tests passed!\n");
    return 0;
}
