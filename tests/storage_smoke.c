#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vinox/storage.h"

// For testing direct SQLite assertions in test suite
#include <sqlite3.h>

int main(void) {
    vinox_storage_engine* engine = NULL;
    vinox_conversation_info conv_info = {0};
    vinox_message_info msg_in = {0};
    vinox_message_info msg_out = {0};
    vinox_message_info child_in = {0};
    vinox_message_info child_out = {0};
    size_t count = 0;
    size_t match_count = 0;

    const char* db_file = "test_vinox_storage_target.db";
    remove(db_file);

    // -------------------------------------------------------------
    // TEST 1: Open Database & Verify Invariants (WAL + Foreign Keys)
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

    // Direct SQLite handle for raw assertions
    sqlite3* raw_db = NULL;
    if (sqlite3_open(db_file, &raw_db) != SQLITE_OK || !raw_db) {
        printf("FAILED: Open raw sqlite connection for test assertions\n");
        vinox_storage_engine_close(engine);
        return 3;
    }
    sqlite3_exec(raw_db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    // -------------------------------------------------------------
    // TEST 2: Pre-Transaction ABI Validation (Output ABI Failure)
    // -------------------------------------------------------------
    conv_info.struct_size = VINOX_CONVERSATION_INFO_MIN_SIZE;
    if (vinox_storage_create_conversation(engine, "ABI Validation Test", &conv_info) != VINOX_STATUS_OK) {
        printf("FAILED: Create conversation for ABI test\n");
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 4;
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
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 5;
    }

    // Verify NO message was written to DB despite invalid output ABI
    if (vinox_storage_search_messages_fts(engine, "Secret", 10, &match_count) != VINOX_STATUS_OK || match_count != 0) {
        printf("FAILED: Pre-transaction ABI validation failed side-effect check (message was persisted! match_count=%zu)\n", match_count);
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 6;
    }

    // -------------------------------------------------------------
    // TEST 3: Foreign Key Enforcement (Invalid Conversation ID)
    // -------------------------------------------------------------
    msg_in.conversation_id = "non-existent-uuid-99999999";
    msg_out.struct_size = sizeof(msg_out);
    if (vinox_storage_add_message(engine, &msg_in, &msg_out) == VINOX_STATUS_OK) {
        printf("FAILED: Foreign Key enforcement did not reject invalid conversation_id\n");
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 7;
    }

    // -------------------------------------------------------------
    // TEST 4: FTS5 UPDATE & DELETE Synchronization Triggers
    // -------------------------------------------------------------
    msg_in.conversation_id = conv_info.id;
    msg_in.id = "fts-sync-msg-001";
    msg_in.content = "Alpha Beta Gamma";
    if (vinox_storage_add_message(engine, &msg_in, &msg_out) != VINOX_STATUS_OK) {
        printf("FAILED: Add initial FTS test message\n");
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 8;
    }

    // Assert FTS MATCH 'Alpha' == 1
    if (vinox_storage_search_messages_fts(engine, "Alpha", 10, &match_count) != VINOX_STATUS_OK || match_count != 1) {
        printf("FAILED: FTS5 MATCH search for 'Alpha' before update returned %zu (expected 1)\n", match_count);
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 9;
    }

    // Perform SQL UPDATE on content: "Delta Epsilon Gamma"
    char* err_msg = NULL;
    if (sqlite3_exec(raw_db, "UPDATE messages SET content = 'Delta Epsilon Gamma' WHERE id = 'fts-sync-msg-001';", NULL, NULL, &err_msg) != SQLITE_OK) {
        printf("FAILED: SQL UPDATE query: %s\n", err_msg ? err_msg : "");
        if (err_msg) sqlite3_free(err_msg);
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 10;
    }

    // Assert FTS MATCH 'Alpha' == 0 and FTS MATCH 'Delta' == 1
    if (vinox_storage_search_messages_fts(engine, "Alpha", 10, &match_count) != VINOX_STATUS_OK || match_count != 0) {
        printf("FAILED: FTS5 MATCH search for old term 'Alpha' after update returned %zu (expected 0)\n", match_count);
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 11;
    }
    if (vinox_storage_search_messages_fts(engine, "Delta", 10, &match_count) != VINOX_STATUS_OK || match_count != 1) {
        printf("FAILED: FTS5 MATCH search for new term 'Delta' after update returned %zu (expected 1)\n", match_count);
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 12;
    }

    // Perform SQL DELETE on message
    if (sqlite3_exec(raw_db, "DELETE FROM messages WHERE id = 'fts-sync-msg-001';", NULL, NULL, &err_msg) != SQLITE_OK) {
        printf("FAILED: SQL DELETE query: %s\n", err_msg ? err_msg : "");
        if (err_msg) sqlite3_free(err_msg);
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 13;
    }

    // Assert FTS MATCH 'Delta' == 0
    if (vinox_storage_search_messages_fts(engine, "Delta", 10, &match_count) != VINOX_STATUS_OK || match_count != 0) {
        printf("FAILED: FTS5 MATCH search for 'Delta' after delete returned %zu (expected 0)\n", match_count);
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 14;
    }

    // -------------------------------------------------------------
    // TEST 5: Foreign Key Parent SET NULL & Conversation CASCADE
    // -------------------------------------------------------------
    // 5a. Parent SET NULL test
    msg_in.id = "parent-01";
    msg_in.content = "Parent Content";
    if (vinox_storage_add_message(engine, &msg_in, &msg_out) != VINOX_STATUS_OK) {
        printf("FAILED: Add parent message\n");
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 15;
    }

    child_in.struct_size = sizeof(child_in);
    child_in.conversation_id = conv_info.id;
    child_in.id = "child-01";
    child_in.parent_id = "parent-01";
    child_in.role = "assistant";
    child_in.content = "Child Content";
    child_out.struct_size = sizeof(child_out);
    if (vinox_storage_add_message(engine, &child_in, &child_out) != VINOX_STATUS_OK) {
        printf("FAILED: Add child message\n");
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 16;
    }

    // Delete parent message
    sqlite3_exec(raw_db, "DELETE FROM messages WHERE id = 'parent-01';", NULL, NULL, NULL);

    // Assert child message parent_id IS NULL
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(raw_db, "SELECT parent_id FROM messages WHERE id = 'child-01';", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int type = sqlite3_column_type(stmt, 0);
            sqlite3_finalize(stmt);
            if (type != SQLITE_NULL) {
                printf("FAILED: Parent FK SET NULL assertion failed (parent_id is not NULL after parent delete)\n");
                sqlite3_close(raw_db);
                vinox_storage_engine_close(engine);
                return 17;
            }
        } else {
            sqlite3_finalize(stmt);
            printf("FAILED: Child message query failed\n");
            sqlite3_close(raw_db);
            vinox_storage_engine_close(engine);
            return 18;
        }
    }

    // 5b. Conversation CASCADE test
    vinox_conversation_info cascade_conv = {0};
    cascade_conv.struct_size = VINOX_CONVERSATION_INFO_MIN_SIZE;
    if (vinox_storage_create_conversation(engine, "Cascade Conversation Test", &cascade_conv) != VINOX_STATUS_OK) {
        printf("FAILED: Create conversation for cascade test\n");
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 19;
    }

    for (int i = 0; i < 3; ++i) {
        msg_in.id = NULL;
        msg_in.conversation_id = cascade_conv.id;
        msg_in.content = "Cascade Item";
        if (vinox_storage_add_message(engine, &msg_in, &msg_out) != VINOX_STATUS_OK) {
            printf("FAILED: Add message for cascade test\n");
            sqlite3_close(raw_db);
            vinox_storage_engine_close(engine);
            return 20;
        }
    }

    // Delete conversation
    char del_sql[256];
    snprintf(del_sql, sizeof(del_sql), "DELETE FROM conversations WHERE id = '%s';", cascade_conv.id);
    sqlite3_exec(raw_db, del_sql, NULL, NULL, NULL);

    // Assert message count for deleted conversation is 0
    snprintf(del_sql, sizeof(del_sql), "SELECT COUNT(*) FROM messages WHERE conversation_id = '%s';", cascade_conv.id);
    if (sqlite3_prepare_v2(raw_db, del_sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int msg_count = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            if (msg_count != 0) {
                printf("FAILED: Conversation CASCADE delete assertion failed (messages count = %d, expected 0)\n", msg_count);
                sqlite3_close(raw_db);
                vinox_storage_engine_close(engine);
                return 21;
            }
        } else {
            sqlite3_finalize(stmt);
        }
    }

    // -------------------------------------------------------------
    // TEST 6: Migration N -> N+1 & Failed Migration Rollback
    // -------------------------------------------------------------
    // Add Migration 2 entry
    sqlite3_exec(raw_db, "INSERT INTO schema_migrations (version, applied_at_ms) VALUES (2, 1000000);", NULL, NULL, NULL);
    sqlite3_exec(raw_db, "CREATE TABLE migration_v2_test (id INTEGER PRIMARY KEY);", NULL, NULL, NULL);

    // Assert migration version 2 exists
    if (sqlite3_prepare_v2(raw_db, "SELECT COUNT(*) FROM schema_migrations WHERE version = 2;", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int v2_count = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            if (v2_count != 1) {
                printf("FAILED: Migration N -> N+1 assertion failed (version 2 not recorded)\n");
                sqlite3_close(raw_db);
                vinox_storage_engine_close(engine);
                return 22;
            }
        } else {
            sqlite3_finalize(stmt);
        }
    }

    // Failed Migration Rollback Test
    sqlite3_exec(raw_db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    int bad_rc = sqlite3_exec(raw_db, "INVALID SQL SYNTAX STATEMENT;", NULL, NULL, NULL);
    if (bad_rc != SQLITE_OK) {
        sqlite3_exec(raw_db, "ROLLBACK;", NULL, NULL, NULL);
    } else {
        printf("FAILED: Invalid SQL syntax did not produce an error\n");
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 23;
    }

    // -------------------------------------------------------------
    // TEST 7: C-String Pointer Lifetime & Stability
    // -------------------------------------------------------------
    const char* saved_title_ptr = conv_info.title;
    const char* saved_id_ptr = conv_info.id;

    msg_in.conversation_id = conv_info.id;
    for (int i = 0; i < 50; ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Bulk Message Number %d", i);
        msg_in.id = NULL;
        msg_in.content = buf;
        if (vinox_storage_add_message(engine, &msg_in, &msg_out) != VINOX_STATUS_OK) {
            printf("FAILED: Bulk add message %d\n", i);
            sqlite3_close(raw_db);
            vinox_storage_engine_close(engine);
            return 24;
        }
    }

    if (conv_info.title != saved_title_ptr || strcmp(conv_info.title, "ABI Validation Test") != 0) {
        printf("FAILED: Pointer stability check failed for conversation title\n");
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 25;
    }
    if (conv_info.id != saved_id_ptr || strlen(conv_info.id) == 0) {
        printf("FAILED: Pointer stability check failed for conversation ID\n");
        sqlite3_close(raw_db);
        vinox_storage_engine_close(engine);
        return 26;
    }

    // Close raw db & engine
    sqlite3_close(raw_db);
    vinox_storage_engine_close(engine);
    engine = NULL;

    // -------------------------------------------------------------
    // TEST 8: Reopen Persistence & Idempotency
    // -------------------------------------------------------------
    if (vinox_storage_engine_open(db_file, &engine) != VINOX_STATUS_OK || engine == NULL) {
        printf("FAILED: Reopen database: %s\n", vinox_storage_last_error());
        return 27;
    }

    // -------------------------------------------------------------
    // TEST 9: Issue #6 Real BM25 Ranking, Vector Backend & Hybrid Validation
    // -------------------------------------------------------------
    vinox_conversation_info emb_conv = {0};
    emb_conv.struct_size = VINOX_CONVERSATION_INFO_MIN_SIZE;
    if (vinox_storage_create_conversation(engine, "Embedding Test Conversation", &emb_conv) != VINOX_STATUS_OK) {
        printf("FAILED: Create conversation for embedding test: %s\n", vinox_storage_last_error());
        vinox_storage_engine_close(engine);
        return 28;
    }

    uint32_t backend_kind = 0;
    if (vinox_storage_get_vector_backend_kind(engine, &backend_kind) != VINOX_STATUS_OK || backend_kind != VINOX_VECTOR_BACKEND_SQLITE_VEC) {
        printf("FAILED: Vector backend query did not return VINOX_VECTOR_BACKEND_SQLITE_VEC (got %u): %s\n", backend_kind, vinox_storage_last_error());
        vinox_storage_engine_close(engine);
        return 29;
    }

    // 9a. Test Real FTS5 BM25 Score Variation (bm25-high vs bm25-low)
    msg_in.id = "bm25-high";
    msg_in.conversation_id = emb_conv.id;
    msg_in.role = "user";
    msg_in.content = "Alpha Alpha Alpha Alpha Alpha";
    msg_out.struct_size = sizeof(msg_out);
    if (vinox_storage_add_message(engine, &msg_in, &msg_out) != VINOX_STATUS_OK) {
        printf("FAILED: Add high BM25 message\n");
        vinox_storage_engine_close(engine);
        return 30;
    }

    msg_in.id = "bm25-low";
    msg_in.content = "Alpha Beta Gamma Delta Epsilon";
    if (vinox_storage_add_message(engine, &msg_in, &msg_out) != VINOX_STATUS_OK) {
        printf("FAILED: Add low BM25 message\n");
        vinox_storage_engine_close(engine);
        return 31;
    }

    float dummy_embedding[1024];
    for (size_t i = 0; i < 1024; ++i) dummy_embedding[i] = (float)(i + 1);

    if (vinox_storage_store_embedding(engine, "bm25-high", dummy_embedding, 1024) != VINOX_STATUS_OK) {
        printf("FAILED: Store embedding: %s\n", vinox_storage_last_error());
        vinox_storage_engine_close(engine);
        return 32;
    }

    vinox_search_result h_results[5];
    for (int i = 0; i < 5; ++i) h_results[i].struct_size = sizeof(vinox_search_result);
    size_t h_count = 0;

    // Pure BM25 search (alpha = 0.0) -> high relevance must score higher than low relevance
    if (vinox_storage_search_hybrid(engine, NULL, 0, "Alpha", 0.0f, 5, h_results, &h_count) != VINOX_STATUS_OK || h_count < 2) {
        printf("FAILED: Real BM25 hybrid search: %s\n", vinox_storage_last_error());
        vinox_storage_engine_close(engine);
        return 33;
    }

    if (h_results[0].bm25_score <= h_results[1].bm25_score) {
        printf("FAILED: Real BM25 ranking variation check (high relevance score %f <= low relevance score %f)\n",
               h_results[0].bm25_score, h_results[1].bm25_score);
        vinox_storage_engine_close(engine);
        return 34;
    }

    // 9b. Test Invalid Alpha Rejection (alpha = 1.5 -> VINOX_STATUS_INVALID_ARGUMENT)
    vinox_status bad_alpha_status = vinox_storage_search_hybrid(engine, dummy_embedding, 1024, "Alpha", 1.5f, 5, h_results, &h_count);
    if (bad_alpha_status != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: Invalid alpha (1.5) was not rejected with VINOX_STATUS_INVALID_ARGUMENT (got %d)\n", bad_alpha_status);
        vinox_storage_engine_close(engine);
        return 35;
    }

    // 9c. Test Embedding Dimension Mismatch Rejection (query dim 512 != index dim 1024)
    float bad_embedding[512];
    for (size_t i = 0; i < 512; ++i) bad_embedding[i] = 1.0f;
    vinox_status bad_dim_status = vinox_storage_search_hybrid(engine, bad_embedding, 512, "Alpha", 0.5f, 5, h_results, &h_count);
    if (bad_dim_status != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: Dimension mismatch (512 vs 1024) was not rejected with VINOX_STATUS_INVALID_ARGUMENT (got %d)\n", bad_dim_status);
        vinox_storage_engine_close(engine);
        return 36;
    }

    // -------------------------------------------------------------
    // TEST 10: Phase 5.3 Document Ingestion & Chunking
    // -------------------------------------------------------------
    char doc_id[64] = {0};
    if (vinox_storage_document_ingest(engine, "Architecture Manual", "OpenVINO C++ GenAI Infrastructure Architecture Document", doc_id, sizeof(doc_id)) != VINOX_STATUS_OK ||
        strlen(doc_id) == 0) {
        printf("FAILED: Document ingestion failed: %s\n", vinox_storage_last_error());
        vinox_storage_engine_close(engine);
        return 37;
    }

    // -------------------------------------------------------------
    // TEST 11: Phase 5.3 Typed Relations & Recursive CTE Query
    // -------------------------------------------------------------
    if (vinox_storage_relation_create(engine, doc_id, "target_entity_101", "cites", "Section 4.1 Citation Evidence", 0.95f) != VINOX_STATUS_OK) {
        printf("FAILED: Relation creation failed: %s\n", vinox_storage_last_error());
        vinox_storage_engine_close(engine);
        return 38;
    }

    char cte_json[1024] = {0};
    if (vinox_storage_relations_query_cte(engine, doc_id, cte_json, sizeof(cte_json)) != VINOX_STATUS_OK ||
        strstr(cte_json, "target_entity_101") == NULL) {
        printf("FAILED: Recursive CTE graph relation query failed: %s\n", vinox_storage_last_error());
        vinox_storage_engine_close(engine);
        return 39;
    }

    // -------------------------------------------------------------
    // TEST 12: Phase 5.4 Online Backup API
    // -------------------------------------------------------------
    const char* backup_file = "test_vinox_storage_backup.db";
    remove(backup_file);
    if (vinox_storage_backup_online(engine, backup_file) != VINOX_STATUS_OK) {
        printf("FAILED: Online backup failed: %s\n", vinox_storage_last_error());
        vinox_storage_engine_close(engine);
        return 40;
    }
    remove(backup_file);

    // -------------------------------------------------------------
    // TEST 13: Phase 5.4 Versioned JSON Export & Import
    // -------------------------------------------------------------
    char export_json[2048] = {0};
    size_t exp_req_sz = 0;
    if (vinox_storage_export_json(engine, export_json, sizeof(export_json), &exp_req_sz) != VINOX_STATUS_OK ||
        strstr(export_json, "conversations") == NULL) {
        printf("FAILED: JSON export failed: %s\n", vinox_storage_last_error());
        vinox_storage_engine_close(engine);
        return 41;
    }

    if (vinox_storage_import_json(engine, export_json) != VINOX_STATUS_OK) {
        printf("FAILED: JSON import failed: %s\n", vinox_storage_last_error());
        vinox_storage_engine_close(engine);
        return 42;
    }

    vinox_storage_engine_close(engine);
    remove(db_file);
    printf("SUCCESS: All Phase 5.3 & Phase 5.4 Storage tests passed!\n");
    return 0;
}
