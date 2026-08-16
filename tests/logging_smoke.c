#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vinox/logging.h"
#include "vinox/vinox.h"

int main(void) {
    printf("Starting VINOX Logging & Correlation Smoke Test...\n");

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

    // 2. Test Secret Redaction (Authorization Bearer & API key)
    const char* secret_input = "Authorization: Bearer sk-1234567890abcdef";
    char redacted_buf[256];
    size_t req_size = 0;
    if (vinox_redact_sensitive_text(secret_input, redacted_buf, sizeof(redacted_buf), &req_size) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_redact_sensitive_text\n");
        return 3;
    }

    if (strstr(redacted_buf, "sk-1234567890abcdef") != NULL || strstr(redacted_buf, "[REDACTED]") == NULL) {
        printf("FAILED: Secret redaction failed to obscure bearer token/API key (got '%s')\n", redacted_buf);
        return 4;
    }

    // 3. Test File Sink Configuration & Structured Logging Event with Correlation Context
    const char* test_log_path = "test_vinox_smoke.log";
    remove(test_log_path);

    if (vinox_log_configure_sink(test_log_path, 10, 3) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_log_configure_sink\n");
        return 5;
    }

    vinox_correlation_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.struct_size = sizeof(ctx);
    ctx.request_id = "req-test-999";
    ctx.session_id = "sess-test-888";
    ctx.run_id = "run-test-777";

    if (vinox_log_event(VINOX_LOG_INFO, "storage", "test.message.add", &ctx, "Operation payload details") != VINOX_STATUS_OK) {
        printf("FAILED: vinox_log_event\n");
        return 6;
    }

    // Flush and close file sink
    vinox_log_configure_sink(NULL, 0, 0);

    // Read back log file and verify json envelope & correlation propagation
    FILE* f = fopen(test_log_path, "r");
    if (!f) {
        printf("FAILED: Could not open written log file '%s'\n", test_log_path);
        return 7;
    }

    char file_line[1024];
    if (!fgets(file_line, sizeof(file_line), f)) {
        printf("FAILED: Could not read log line from '%s'\n", test_log_path);
        fclose(f);
        return 8;
    }
    fclose(f);
    remove(test_log_path);

    if (strstr(file_line, "\"event_schema_version\":1") == NULL ||
        strstr(file_line, "\"request_id\":\"req-test-999\"") == NULL ||
        strstr(file_line, "\"component\":\"storage\"") == NULL) {
        printf("FAILED: Log line structured JSON envelope verification (got '%s')\n", file_line);
        return 9;
    }

    printf("SUCCESS: All VINOX Logging & Secret Redaction smoke tests passed!\n");
    return 0;
}
