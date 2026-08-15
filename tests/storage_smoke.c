#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vinox/storage.h"

int main(void) {
    vinox_storage_engine* engine = NULL;
    vinox_conversation_info conv_info = {0};
    vinox_message_info msg_in = {0};
    vinox_message_info msg_out = {0};
    size_t count = 0;
    size_t match_count = 0;

    const char* db_file = "test_vinox_storage_issue5.db";
    remove(db_file);

    // -------------------------------------------------------------
    // TEST 1: Open database & Invariants Enforcement
    // -------------------------------------------------------------
    if (vinox_storage_engine_open(db_file, &engine) != VINOX_STATUS_OK || engine == NULL) {
        printf("FAILED: Open database: %s\n", vinox_storage_last_error());
        return 1;
    }

    if (vinox_storage_get_conversation_count(engine, &count) != VINOX_STATUS_OK || count != 0) {
        printf("FAILED: Empty DB conversation count\n");
        vinox_storage_engine_close(engine);
        return 2;
    }

    // -------------------------------------------------------------
    // TEST 2: Pre-Transaction ABI Validation (Output ABI Failure)
    // -------------------------------------------------------------
    conv_info.struct_size = VINOX_CONVERSATION_INFO_MIN_SIZE;
    if (vinox_storage_create_conversation(engine, "ABI Validation Test", &conv_info) != VINOX_STATUS_OK) {
        printf("FAILED: Create conversation for ABI test\n");
        vinox_storage_engine_close(engine);
        return 3;
    }

    msg_in.struct_size = sizeof(msg_in);
    msg_in.conversation_id = conv_info.id;
    msg_in.role = "user";
    msg_in.content = "Secret Message Should Not Be Written";
    msg_in.provenance_kind = VINOX_PROVENANCE_SOURCE_LITERAL;

    // Provide invalid struct_size in message_out
    msg_out.struct_size = 4; // Smaller than VINOX_MESSAGE_INFO_MIN_SIZE!

    vinox_status abi_status = vinox_storage_add_message(engine, &msg_in, &msg_out);
    if (abi_status != VINOX_STATUS_INCOMPATIBLE_ABI) {
        printf("FAILED: Pre-transaction ABI check did not return VINOX_STATUS_INCOMPATIBLE_ABI (got %d)\n", abi_status);
        vinox_storage_engine_close(engine);
        return 4;
    }

    // Verify NO message was written to DB despite invalid output ABI
    if (vinox_storage_search_messages_fts(engine, "Secret", 10, &match_count) != VINOX_STATUS_OK || match_count != 0) {
        printf("FAILED: Pre-transaction ABI validation failed side-effect check (message was persisted! match_count=%zu)\n", match_count);
        vinox_storage_engine_close(engine);
        return 5;
    }

    // -------------------------------------------------------------
    // TEST 3: Foreign Key Enforcement
    // -------------------------------------------------------------
    msg_in.conversation_id = "non-existent-conversation-uuid-9999";
    msg_out.struct_size = sizeof(msg_out);
    vinox_status fk_status = vinox_storage_add_message(engine, &msg_in, &msg_out);
    if (fk_status == VINOX_STATUS_OK) {
        printf("FAILED: Foreign Key enforcement did not reject invalid conversation_id\n");
        vinox_storage_engine_close(engine);
        return 6;
    }

    // -------------------------------------------------------------
    // TEST 4: Real FTS5 MATCH Search & Trigger Synchronization
    // -------------------------------------------------------------
    msg_in.conversation_id = conv_info.id;
    msg_in.content = "OpenVINO GenAI Infrastructure for C++";
    if (vinox_storage_add_message(engine, &msg_in, &msg_out) != VINOX_STATUS_OK) {
        printf("FAILED: Add valid message\n");
        vinox_storage_engine_close(engine);
        return 7;
    }

    // FTS5 MATCH query for "OpenVINO"
    if (vinox_storage_search_messages_fts(engine, "OpenVINO", 10, &match_count) != VINOX_STATUS_OK || match_count != 1) {
        printf("FAILED: FTS5 MATCH search for 'OpenVINO' returned %zu matches (expected 1)\n", match_count);
        vinox_storage_engine_close(engine);
        return 8;
    }

    // FTS5 MATCH query for non-existent word "Quantum"
    if (vinox_storage_search_messages_fts(engine, "Quantum", 10, &match_count) != VINOX_STATUS_OK || match_count != 0) {
        printf("FAILED: FTS5 MATCH search for 'Quantum' returned %zu matches (expected 0)\n", match_count);
        vinox_storage_engine_close(engine);
        return 9;
    }

    // -------------------------------------------------------------
    // TEST 5: Close & Reopen Persistence & Idempotency
    // -------------------------------------------------------------
    vinox_storage_engine_close(engine);
    // 1. Open database
    if (vinox_storage_engine_open(db_file, &engine) != VINOX_STATUS_OK || engine == NULL) {
        printf("FAILED: Open database: %s\n", vinox_storage_last_error());
        return 1;
    }

    if (vinox_storage_get_conversation_count(engine, &count) != VINOX_STATUS_OK || count != 1) {
        printf("FAILED: Reopened DB conversation count (expected 1, got %zu)\n", count);
        vinox_storage_engine_close(engine);
        return 11;
    }

    if (vinox_storage_search_messages_fts(engine, "OpenVINO", 10, &match_count) != VINOX_STATUS_OK || match_count != 1) {
        printf("FAILED: Reopened DB FTS5 MATCH search for 'OpenVINO' returned %zu matches (expected 1)\n", match_count);
        vinox_storage_engine_close(engine);
        return 12;
    }

    vinox_storage_engine_close(engine);
    remove(db_file);
    printf("SUCCESS: All Issue #5 Storage tests passed!\n");
    return 0;
}
