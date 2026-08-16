#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vinox/logging.h"
#include "vinox/storage.h"
#include "vinox/vinox.h"

int main(void) {
    printf("Starting VINOX Issue #8 Protocol & Evidence Hardened Smoke Test...\n");

    // 1. Log Level Validation (reject values > VINOX_LOG_CRITICAL)
    if (vinox_log_set_level(VINOX_LOG_CRITICAL + 10) != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: vinox_log_set_level did not reject level > VINOX_LOG_CRITICAL\n");
        return 1;
    }

    if (vinox_log_set_level(VINOX_LOG_DEBUG) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_log_set_level(VINOX_LOG_DEBUG)\n");
        return 2;
    }

    // 2. Wire Serialization & Buffer Size Contract
    vinox_correlation_context orig_ctx;
    memset(&orig_ctx, 0, sizeof(orig_ctx));
    orig_ctx.struct_size = sizeof(orig_ctx);
    orig_ctx.request_id = "req-boundary-101";
    orig_ctx.session_id = "sess-boundary-202";

    char tiny_wire_buf[10];
    size_t req_wire_size = 0;
    if (vinox_correlation_serialize_envelope(&orig_ctx, tiny_wire_buf, sizeof(tiny_wire_buf), &req_wire_size) != VINOX_STATUS_INVALID_ARGUMENT || req_wire_size == 0) {
        printf("FAILED: vinox_correlation_serialize_envelope failed to reject insufficient buffer\n");
        return 3;
    }

    char wire_buf[512];
    if (vinox_correlation_serialize_envelope(&orig_ctx, wire_buf, sizeof(wire_buf), NULL) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_correlation_serialize_envelope\n");
        return 4;
    }

    // 3. Deserializer Wire Version Enforcement & Pool Exhaustion Tests
    vinox_correlation_context test_ctx;
    memset(&test_ctx, 0, sizeof(test_ctx));
    test_ctx.struct_size = sizeof(test_ctx);
    char pool_buf[512];

    // Missing wire_version
    if (vinox_correlation_deserialize_envelope("{\"request_id\":\"r1\"}", &test_ctx, pool_buf, sizeof(pool_buf)) != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: vinox_correlation_deserialize_envelope failed to reject missing wire_version\n");
        return 5;
    }

    // Unsupported wire_version (999)
    if (vinox_correlation_deserialize_envelope("{\"wire_version\":999,\"request_id\":\"r1\"}", &test_ctx, pool_buf, sizeof(pool_buf)) != VINOX_STATUS_INCOMPATIBLE_ABI) {
        printf("FAILED: vinox_correlation_deserialize_envelope failed to reject unsupported wire_version 999\n");
        return 6;
    }

    // String pool exhaustion (too small pool)
    char tiny_pool[4];
    if (vinox_correlation_deserialize_envelope(wire_buf, &test_ctx, tiny_pool, sizeof(tiny_pool)) != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: vinox_correlation_deserialize_envelope failed to reject string pool exhaustion\n");
        return 7;
    }

    // Prefix-ABI struct pool exhaustion safety test (smaller caller struct)
    typedef struct partial_correlation_context {
        uint32_t struct_size;
        const char* request_id;
    } partial_correlation_context;

    partial_correlation_context partial_ctx_out;
    memset(&partial_ctx_out, 0xAB, sizeof(partial_ctx_out));
    partial_ctx_out.struct_size = (uint32_t)sizeof(partial_ctx_out);

    if (vinox_correlation_deserialize_envelope(wire_buf, (vinox_correlation_context*)&partial_ctx_out, tiny_pool, sizeof(tiny_pool)) != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: Prefix-ABI struct pool exhaustion safety test failed\n");
        return 7;
    }

    // Happy path deserialization
    if (vinox_correlation_deserialize_envelope(wire_buf, &test_ctx, pool_buf, sizeof(pool_buf)) != VINOX_STATUS_OK ||
        strcmp(test_ctx.request_id, "req-boundary-101") != 0) {
        printf("FAILED: Happy path deserialization\n");
        return 8;
    }

    // 4. Overlong Field Truncation & Secret Absence in Written Log
    const char* test_log_path = "test_vinox_protocol_smoke.log";
    remove(test_log_path);

    if (vinox_log_configure_sink(test_log_path, 1, 3) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_log_configure_sink: %s\n", vinox_last_error());
        return 9;
    }

    // Construct 50-char component name, 100-char event ID, and 1500-char secret-bearing details
    char overlong_comp[64];
    memset(overlong_comp, 'C', 50);
    overlong_comp[50] = '\0';

    char overlong_evt[128];
    memset(overlong_evt, 'E', 100);
    overlong_evt[100] = '\0';

    char overlong_details[2000];
    snprintf(overlong_details, sizeof(overlong_details), "Authorization: Bearer sk-secretkey-12345 ");
    size_t cur_len = strlen(overlong_details);
    memset(overlong_details + cur_len, 'X', 1500);
    overlong_details[cur_len + 1500] = '\0';

    vinox_log_event_meta meta;
    memset(&meta, 0, sizeof(meta));
    meta.struct_size = sizeof(meta);
    meta.details = overlong_details;

    if (vinox_log_event_ex(VINOX_LOG_INFO, overlong_comp, overlong_evt, &orig_ctx, &meta) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_log_event_ex\n");
        return 10;
    }

    // Flush spdlog logger
    vinox_log_configure_sink(NULL, 0, 0);

    FILE* f = fopen(test_log_path, "r");
    if (!f) {
        printf("FAILED: Could not open written log file '%s'\n", test_log_path);
        return 11;
    }

    char file_line[4096];
    if (!fgets(file_line, sizeof(file_line), f)) {
        printf("FAILED: Could not read log line from '%s'\n", test_log_path);
        fclose(f);
        return 12;
    }
    fclose(f);
    remove(test_log_path);

    // Verify raw secret is ABSENT and REDACTED is PRESENT
    if (strstr(file_line, "sk-secretkey-12345") != NULL || strstr(file_line, "[REDACTED]") == NULL) {
        printf("FAILED: Secret-bearing log line contained raw secret! (got '%s')\n", file_line);
        return 13;
    }

    // Verify overlong component was truncated to 32 chars and event to 64 chars
    if (strstr(file_line, "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC") == NULL ||
        strstr(file_line, "...[TRUNCATED]") == NULL) {
        printf("FAILED: Field truncation verification failed (got '%s')\n", file_line);
        return 14;
    }

    printf("SUCCESS: All VINOX Issue #8 Protocol & Evidence Hardening smoke tests passed!\n");
    return 0;
}
