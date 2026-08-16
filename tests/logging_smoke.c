#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vinox/logging.h"
#include "vinox/storage.h"
#include "vinox/vinox.h"

int main(void) {
    printf("Starting VINOX Issue #8 Comprehensive Acceptance Smoke Test...\n");

    // 1. Test Log Level Validation (reject values > VINOX_LOG_CRITICAL)
    if (vinox_log_set_level(VINOX_LOG_CRITICAL + 10) != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: vinox_log_set_level did not reject level > VINOX_LOG_CRITICAL\n");
        return 1;
    }

    if (vinox_log_set_level(VINOX_LOG_DEBUG) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_log_set_level(VINOX_LOG_DEBUG)\n");
        return 2;
    }

    uint32_t active_level = 0;
    if (vinox_log_get_level(&active_level) != VINOX_STATUS_OK || active_level != VINOX_LOG_DEBUG) {
        printf("FAILED: vinox_log_get_level (expected %d, got %u)\n", VINOX_LOG_DEBUG, active_level);
        return 3;
    }

    // 2. Test Process-Boundary Envelope Serialization & Deserialization
    vinox_correlation_context orig_ctx;
    memset(&orig_ctx, 0, sizeof(orig_ctx));
    orig_ctx.struct_size = sizeof(orig_ctx);
    orig_ctx.request_id = "req-boundary-101";
    orig_ctx.session_id = "sess-boundary-202";
    orig_ctx.run_id = "run-boundary-303";

    char wire_buf[512];
    size_t req_wire_size = 0;
    if (vinox_correlation_serialize_envelope(&orig_ctx, wire_buf, sizeof(wire_buf), &req_wire_size) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_correlation_serialize_envelope\n");
        return 4;
    }

    if (strstr(wire_buf, "\"request_id\":\"req-boundary-101\"") == NULL || strstr(wire_buf, "\"wire_version\":1") == NULL) {
        printf("FAILED: Serialized wire format verification (got '%s')\n", wire_buf);
        return 5;
    }

    vinox_correlation_context deserialized_ctx;
    memset(&deserialized_ctx, 0, sizeof(deserialized_ctx));
    deserialized_ctx.struct_size = sizeof(deserialized_ctx);
    char pool_buf[512];

    if (vinox_correlation_deserialize_envelope(wire_buf, &deserialized_ctx, pool_buf, sizeof(pool_buf)) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_correlation_deserialize_envelope\n");
        return 6;
    }

    if (strcmp(deserialized_ctx.request_id, "req-boundary-101") != 0 ||
        strcmp(deserialized_ctx.session_id, "sess-boundary-202") != 0 ||
        strcmp(deserialized_ctx.run_id, "run-boundary-303") != 0) {
        printf("FAILED: Deserialized correlation values do not match original\n");
        return 7;
    }

    // 3. Test Typed Canonical Structured Log Event (vinox_log_event_ex) & Spdlog Rotation Sink
    const char* test_log_path = "test_vinox_comprehensive_logging.log";
    remove(test_log_path);

    if (vinox_log_configure_sink(test_log_path, 1, 3) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_log_configure_sink: %s\n", vinox_last_error());
        return 8;
    }

    vinox_log_event_meta typed_meta;
    memset(&typed_meta, 0, sizeof(typed_meta));
    typed_meta.struct_size = sizeof(typed_meta);
    typed_meta.model_id = "Qwen2.5-1B-Instruct";
    typed_meta.backend = "openvino";
    typed_meta.duration_ms = 42;
    typed_meta.status = "OK";
    typed_meta.status_code = 200;
    typed_meta.details = "Inference completed successfully";

    // Invalid correlation ID chars test (should be sanitized to [a-zA-Z0-9_.-])
    vinox_correlation_context dirty_ctx;
    memset(&dirty_ctx, 0, sizeof(dirty_ctx));
    dirty_ctx.struct_size = sizeof(dirty_ctx);
    dirty_ctx.request_id = "req-123!@#$%^\n\t";

    if (vinox_log_event_ex(VINOX_LOG_INFO, "serving", "inference.complete", &dirty_ctx, &typed_meta) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_log_event_ex\n");
        return 9;
    }

    // 4. Test Cross-DLL Correlation Context Propagation via vinox_storage_add_message_ex
    vinox_storage_engine* storage = NULL;
    const char* db_file = "test_logging_propagation.db";
    remove(db_file);

    if (vinox_storage_engine_open(db_file, &storage) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_storage_engine_open: %s\n", vinox_storage_last_error());
        return 10;
    }

    vinox_conversation_info conv;
    conv.struct_size = sizeof(conv);
    if (vinox_storage_create_conversation(storage, "Cross DLL Logging Test", &conv) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_storage_create_conversation\n");
        vinox_storage_engine_close(storage);
        return 11;
    }

    vinox_message_info msg_in, msg_out;
    memset(&msg_in, 0, sizeof(msg_in));
    msg_in.struct_size = sizeof(msg_in);
    msg_in.conversation_id = conv.id;
    msg_in.role = "user";
    msg_in.content = "Test cross-DLL correlation propagation";
    memset(&msg_out, 0, sizeof(msg_out));
    msg_out.struct_size = sizeof(msg_out);

    vinox_correlation_context dll_ctx;
    memset(&dll_ctx, 0, sizeof(dll_ctx));
    dll_ctx.struct_size = sizeof(dll_ctx);
    dll_ctx.request_id = "req-cross-dll-555";
    dll_ctx.session_id = conv.id;

    if (vinox_storage_add_message_ex(storage, &msg_in, &dll_ctx, &msg_out) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_storage_add_message_ex: %s\n", vinox_storage_last_error());
        vinox_storage_engine_close(storage);
        return 12;
    }

    vinox_storage_engine_close(storage);
    remove(db_file);

    // Flush spdlog logger
    vinox_log_configure_sink(NULL, 0, 0);

    // Verify written log file for typed fields and cross-DLL correlation event
    FILE* f = fopen(test_log_path, "r");
    if (!f) {
        printf("FAILED: Could not open written log file '%s'\n", test_log_path);
        return 13;
    }

    char file_line1[2048];
    char file_line2[2048];
    if (!fgets(file_line1, sizeof(file_line1), f) || !fgets(file_line2, sizeof(file_line2), f)) {
        printf("FAILED: Could not read log lines from '%s'\n", test_log_path);
        fclose(f);
        return 14;
    }
    fclose(f);
    remove(test_log_path);

    // Line 1 checks (vinox_log_event_ex typed fields & sanitized dirty request ID)
    if (strstr(file_line1, "\"model_id\":\"Qwen2.5-1B-Instruct\"") == NULL ||
        strstr(file_line1, "\"backend\":\"openvino\"") == NULL ||
        strstr(file_line1, "\"duration_ms\":42") == NULL ||
        strstr(file_line1, "\"status_code\":200") == NULL ||
        strstr(file_line1, "\"request_id\":\"req-123________\"") == NULL) {
        printf("FAILED: Typed canonical event envelope verification (got '%s')\n", file_line1);
        return 15;
    }

    // Line 2 checks (Cross-DLL storage message.add event with req-cross-dll-555)
    if (strstr(file_line2, "\"component\":\"storage\"") == NULL ||
        strstr(file_line2, "\"event\":\"message.add\"") == NULL ||
        strstr(file_line2, "\"request_id\":\"req-cross-dll-555\"") == NULL) {
        printf("FAILED: Cross-DLL correlation propagation verification (got '%s')\n", file_line2);
        return 16;
    }

    printf("SUCCESS: All VINOX Issue #8 Comprehensive Acceptance smoke tests passed!\n");
    return 0;
}
