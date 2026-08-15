#include "vinox/storage.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <sqlite3.h>

#include "generated/001_init_sql.h"

namespace fs = std::filesystem;

namespace vinox::storage {
void l2_normalize(std::vector<float>& vec);
float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);
}

struct ConversationEntry {
    std::string id;
    std::string title;
    uint64_t created_at_ms{0};
    uint64_t updated_at_ms{0};
};

struct MessageEntry {
    std::string id;
    std::string conversation_id;
    std::string parent_id;
    std::string role;
    std::string content;
    uint32_t provenance_kind{0};
    uint64_t created_at_ms{0};
};

struct vinox_storage_engine {
    sqlite3* db{nullptr};
    std::mutex mutex;
    std::deque<ConversationEntry> conversation_pool;
    std::deque<MessageEntry> message_pool;
    std::deque<std::string> string_pool;
};

namespace {

#define VINOX_FIELD_PRESENT(ptr, member) \
    ((ptr)->struct_size >= (offsetof(std::remove_pointer_t<decltype(ptr)>, member) + sizeof((ptr)->member)))

thread_local std::string last_error;

vinox_status fail_arg(const char* message) {
    last_error = message;
    return VINOX_STATUS_INVALID_ARGUMENT;
}

vinox_status fail_abi(const char* message) {
    last_error = message;
    return VINOX_STATUS_INCOMPATIBLE_ABI;
}

vinox_status fail_runtime(const char* message) {
    last_error = message;
    return VINOX_STATUS_RUNTIME_ERROR;
}

uint64_t current_timestamp_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    std::stringstream ss;
    ss << std::hex << dis(gen) << dis(gen);
    return ss.str().substr(0, 32);
}

bool enforce_sqlite_invariants(sqlite3* db, const char* db_path, std::string& err_out) {
    char* err_msg = nullptr;
    if (sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        err_out = std::string("Failed to enable foreign_keys: ") + (err_msg ? err_msg : "unknown error");
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }

    // Verify Foreign Keys invariant
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA foreign_keys;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int fk_enabled = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            if (fk_enabled != 1) {
                err_out = "SQLite foreign_keys invariant check failed (PRAGMA foreign_keys returned 0)";
                return false;
            }
        } else {
            sqlite3_finalize(stmt);
            err_out = "SQLite foreign_keys query failed to return a row";
            return false;
        }
    } else {
        err_out = "Failed to prepare PRAGMA foreign_keys query";
        return false;
    }

    // Set WAL mode
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);

    // Verify WAL invariant for disk-backed DBs
    std::string path_str(db_path ? db_path : "");
    if (path_str != ":memory:" && path_str.find("mode=memory") == std::string::npos) {
        stmt = nullptr;
        if (sqlite3_prepare_v2(db, "PRAGMA journal_mode;", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* mode = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                std::string mode_str = mode ? mode : "";
                sqlite3_finalize(stmt);
                if (mode_str != "wal" && mode_str != "WAL") {
                    err_out = "SQLite journal_mode invariant check failed (PRAGMA journal_mode returned '" + mode_str + "', expected 'wal')";
                    return false;
                }
            } else {
                sqlite3_finalize(stmt);
                err_out = "SQLite journal_mode query failed to return a row";
                return false;
            }
        } else {
            err_out = "Failed to prepare PRAGMA journal_mode query";
            return false;
        }
    }

    return true;
}

bool run_canonical_migrations(sqlite3* db, std::string& err_out) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS schema_migrations (version INTEGER PRIMARY KEY, applied_at_ms INTEGER NOT NULL);", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        err_out = std::string("Failed to create schema_migrations table: ") + (err_msg ? err_msg : "unknown error");
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }

    // Check version 1 migration
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM schema_migrations WHERE version = 1;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        err_out = std::string("Failed to check migration version: ") + sqlite3_errmsg(db);
        return false;
    }

    bool already_applied = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        already_applied = (sqlite3_column_int(stmt, 0) > 0);
    }
    sqlite3_finalize(stmt);

    if (!already_applied) {
        // Run migration 001 inside a transaction using generated header from schemas/database/001_init.sql
        rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            err_out = std::string("Failed to begin migration transaction: ") + (err_msg ? err_msg : "unknown error");
            if (err_msg) sqlite3_free(err_msg);
            return false;
        }

        rc = sqlite3_exec(db, vinox::storage::CANONICAL_MIGRATION_001_SQL, nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            err_out = std::string("Canonical migration 001 failed: ") + (err_msg ? err_msg : "unknown error");
            if (err_msg) sqlite3_free(err_msg);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        // Record migration 001
        std::string record_sql = "INSERT INTO schema_migrations (version, applied_at_ms) VALUES (1, " + std::to_string(current_timestamp_ms()) + ");";
        rc = sqlite3_exec(db, record_sql.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            err_out = std::string("Failed to record migration 001: ") + (err_msg ? err_msg : "unknown error");
            if (err_msg) sqlite3_free(err_msg);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            err_out = std::string("Failed to commit migration 001: ") + (err_msg ? err_msg : "unknown error");
            if (err_msg) sqlite3_free(err_msg);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }

    return true;
}

}  // namespace

vinox_status vinox_storage_engine_open(
    const char* db_path,
    vinox_storage_engine** engine_out
) {
    if (engine_out == nullptr) {
        return fail_arg("engine_out pointer cannot be null");
    }
    if (db_path == nullptr || db_path[0] == '\0') {
        return fail_arg("db_path cannot be null or empty");
    }

    sqlite3* db = nullptr;
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK || !db) {
        std::string err = db ? sqlite3_errmsg(db) : "Failed to open sqlite database";
        if (db) sqlite3_close(db);
        return fail_runtime(err.c_str());
    }

    std::string inv_err;
    if (!enforce_sqlite_invariants(db, db_path, inv_err)) {
        sqlite3_close(db);
        return fail_runtime(inv_err.c_str());
    }

    std::string mig_err;
    if (!run_canonical_migrations(db, mig_err)) {
        sqlite3_close(db);
        return fail_runtime(mig_err.c_str());
    }

    try {
        auto* engine = new vinox_storage_engine();
        engine->db = db;
        *engine_out = engine;
        last_error.clear();
        return VINOX_STATUS_OK;
    } catch (...) {
        sqlite3_close(db);
        return fail_runtime("Memory allocation error for storage engine");
    }
}

vinox_status vinox_storage_create_conversation(
    vinox_storage_engine* engine,
    const char* title,
    vinox_conversation_info* info_out
) {
    if (engine == nullptr || engine->db == nullptr) {
        return fail_arg("storage engine handle cannot be null");
    }
    if (title == nullptr || title[0] == '\0') {
        return fail_arg("conversation title cannot be null or empty");
    }
    if (info_out == nullptr) {
        return fail_arg("info_out pointer cannot be null");
    }

    // PRE-TRANSACTION ABI VALIDATION
    if (info_out->struct_size < VINOX_CONVERSATION_INFO_MIN_SIZE) {
        return fail_abi("info_out->struct_size is smaller than VINOX_CONVERSATION_INFO_MIN_SIZE");
    }

    std::lock_guard<std::mutex> lock(engine->mutex);

    ConversationEntry entry;
    entry.id = generate_uuid();
    entry.title = title;
    entry.created_at_ms = current_timestamp_ms();
    entry.updated_at_ms = entry.created_at_ms;

    const char* sql = "INSERT INTO conversations (id, title, created_at_ms, updated_at_ms) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail_runtime(sqlite3_errmsg(engine->db));
    }

    sqlite3_bind_text(stmt, 1, entry.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, entry.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(entry.created_at_ms));
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(entry.updated_at_ms));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return fail_runtime(sqlite3_errmsg(engine->db));
    }

    engine->conversation_pool.push_back(entry);
    const auto& stored = engine->conversation_pool.back();

    info_out->id = stored.id.c_str();
    info_out->title = stored.title.c_str();
    if (VINOX_FIELD_PRESENT(info_out, created_at_ms)) info_out->created_at_ms = stored.created_at_ms;
    if (VINOX_FIELD_PRESENT(info_out, updated_at_ms)) info_out->updated_at_ms = stored.updated_at_ms;

    last_error.clear();
    return VINOX_STATUS_OK;
}

vinox_status vinox_storage_add_message(
    vinox_storage_engine* engine,
    const vinox_message_info* message_in,
    vinox_message_info* message_out
) {
    if (engine == nullptr || engine->db == nullptr) {
        return fail_arg("storage engine handle cannot be null");
    }
    if (message_in == nullptr) {
        return fail_arg("message_in pointer cannot be null");
    }

    // PRE-TRANSACTION ABI VALIDATION (INPUT & OUTPUT STRUCTS BEFORE SIDE EFFECTS)
    if (message_in->struct_size < VINOX_MESSAGE_INFO_MIN_SIZE) {
        return fail_abi("message_in->struct_size is smaller than VINOX_MESSAGE_INFO_MIN_SIZE");
    }
    if (message_out != nullptr && message_out->struct_size < VINOX_MESSAGE_INFO_MIN_SIZE) {
        return fail_abi("message_out->struct_size is smaller than VINOX_MESSAGE_INFO_MIN_SIZE");
    }

    if (message_in->conversation_id == nullptr || message_in->content == nullptr || message_in->role == nullptr) {
        return fail_arg("message required fields (conversation_id, role, content) cannot be null");
    }

    std::lock_guard<std::mutex> lock(engine->mutex);

    MessageEntry entry;
    entry.id = (message_in->id && message_in->id[0] != '\0') ? message_in->id : generate_uuid();
    entry.conversation_id = message_in->conversation_id;
    entry.parent_id = (message_in->parent_id && message_in->parent_id[0] != '\0') ? message_in->parent_id : "";
    entry.role = message_in->role;
    entry.content = message_in->content;
    entry.provenance_kind = VINOX_FIELD_PRESENT(message_in, provenance_kind) ? message_in->provenance_kind : 0;
    entry.created_at_ms = VINOX_FIELD_PRESENT(message_in, created_at_ms) && message_in->created_at_ms > 0 ? message_in->created_at_ms : current_timestamp_ms();

    const char* sql = "INSERT INTO messages (id, conversation_id, parent_id, role, content, provenance_kind, created_at_ms) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail_runtime(sqlite3_errmsg(engine->db));
    }

    sqlite3_bind_text(stmt, 1, entry.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, entry.conversation_id.c_str(), -1, SQLITE_STATIC);
    if (!entry.parent_id.empty()) {
        sqlite3_bind_text(stmt, 3, entry.parent_id.c_str(), -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_text(stmt, 4, entry.role.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, entry.content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, static_cast<int>(entry.provenance_kind));
    sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(entry.created_at_ms));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return fail_runtime(sqlite3_errmsg(engine->db));
    }

    engine->message_pool.push_back(entry);
    const auto& stored = engine->message_pool.back();

    if (message_out) {
        message_out->id = stored.id.c_str();
        message_out->conversation_id = stored.conversation_id.c_str();
        message_out->parent_id = stored.parent_id.empty() ? nullptr : stored.parent_id.c_str();
        message_out->role = stored.role.c_str();
        message_out->content = stored.content.c_str();
        if (VINOX_FIELD_PRESENT(message_out, provenance_kind)) message_out->provenance_kind = stored.provenance_kind;
        if (VINOX_FIELD_PRESENT(message_out, created_at_ms)) message_out->created_at_ms = stored.created_at_ms;
    }

    last_error.clear();
    return VINOX_STATUS_OK;
}

vinox_status vinox_storage_get_conversation_count(
    const vinox_storage_engine* engine,
    size_t* count_out
) {
    if (engine == nullptr || engine->db == nullptr) {
        return fail_arg("storage engine handle cannot be null");
    }
    if (count_out == nullptr) {
        return fail_arg("count_out pointer cannot be null");
    }

    std::lock_guard<std::mutex> lock(const_cast<vinox_storage_engine*>(engine)->mutex);

    const char* sql = "SELECT COUNT(*) FROM conversations;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail_runtime(sqlite3_errmsg(engine->db));
    }

    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);

    *count_out = count;
    last_error.clear();
    return VINOX_STATUS_OK;
}

vinox_status vinox_storage_search_messages_fts(
    const vinox_storage_engine* engine,
    const char* query,
    size_t max_results,
    size_t* match_count_out
) {
    if (engine == nullptr || engine->db == nullptr) {
        return fail_arg("storage engine handle cannot be null");
    }
    if (query == nullptr || query[0] == '\0') {
        return fail_arg("search query cannot be null or empty");
    }

    std::lock_guard<std::mutex> lock(const_cast<vinox_storage_engine*>(engine)->mutex);

    // Real FTS5 MATCH Query
    const char* sql = "SELECT COUNT(*) FROM messages_fts WHERE messages_fts MATCH ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail_runtime(sqlite3_errmsg(engine->db));
    }

    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);

    size_t count = 0;
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    } else if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(engine->db);
        sqlite3_finalize(stmt);
        return fail_runtime(err.c_str());
    }
    sqlite3_finalize(stmt);

    if (match_count_out) {
        *match_count_out = (max_results > 0 && count > max_results) ? max_results : count;
    }

    last_error.clear();
    return VINOX_STATUS_OK;
}

vinox_status vinox_storage_store_embedding(
    vinox_storage_engine* engine,
    const char* message_id,
    const float* embedding_data,
    size_t dim
) {
    if (engine == nullptr || engine->db == nullptr) {
        return fail_arg("storage engine handle cannot be null");
    }
    if (message_id == nullptr || message_id[0] == '\0') {
        return fail_arg("message_id cannot be null or empty");
    }
    if (embedding_data == nullptr || dim == 0) {
        return fail_arg("embedding_data cannot be null and dim must be > 0");
    }

    std::vector<float> vec(embedding_data, embedding_data + dim);
    vinox::storage::l2_normalize(vec);

    std::lock_guard<std::mutex> lock(engine->mutex);

    const char* sql = "INSERT OR REPLACE INTO message_embeddings (message_id, embedding, dim, created_at_ms) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail_runtime(sqlite3_errmsg(engine->db));
    }

    sqlite3_bind_text(stmt, 1, message_id, -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, vec.data(), static_cast<int>(vec.size() * sizeof(float)), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, static_cast<int>(dim));
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(current_timestamp_ms()));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return fail_runtime(sqlite3_errmsg(engine->db));
    }

    last_error.clear();
    return VINOX_STATUS_OK;
}

struct HybridCandidate {
    std::string message_id;
    float bm25_score{0.0f};
    float vector_score{0.0f};
    float hybrid_score{0.0f};
};

vinox_status vinox_storage_search_hybrid(
    const vinox_storage_engine* engine,
    const float* query_embedding,
    size_t dim,
    const char* text_query,
    float alpha,
    size_t max_results,
    vinox_search_result* results_out,
    size_t* results_count_out
) {
    if (engine == nullptr || engine->db == nullptr) {
        return fail_arg("storage engine handle cannot be null");
    }
    if (results_out == nullptr && max_results > 0) {
        return fail_arg("results_out cannot be null when max_results > 0");
    }

    // PRE-TRANSACTION ABI VALIDATION ON RESULTS_OUT STRUCTS
    if (results_out != nullptr) {
        for (size_t i = 0; i < max_results; ++i) {
            if (results_out[i].struct_size < VINOX_SEARCH_RESULT_MIN_SIZE) {
                return fail_abi("results_out struct_size is smaller than VINOX_SEARCH_RESULT_MIN_SIZE");
            }
        }
    }

    std::lock_guard<std::mutex> lock(const_cast<vinox_storage_engine*>(engine)->mutex);

    std::vector<float> q_vec;
    if (query_embedding && dim > 0) {
        q_vec.assign(query_embedding, query_embedding + dim);
        vinox::storage::l2_normalize(q_vec);
    }

    std::vector<HybridCandidate> candidates;

    // 1. Fetch text matches via FTS5
    if (text_query && text_query[0] != '\0') {
        const char* sql = "SELECT m.id FROM messages_fts f JOIN messages m ON f.rowid = m.rowid WHERE messages_fts MATCH ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, text_query, -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (id) {
                    HybridCandidate c;
                    c.message_id = id;
                    c.bm25_score = 1.0f;
                    candidates.push_back(c);
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    // 2. Fetch and compute vector similarity for stored embeddings
    if (!q_vec.empty()) {
        const char* sql = "SELECT message_id, embedding, dim FROM message_embeddings;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                const void* blob = sqlite3_column_blob(stmt, 1);
                int bytes = sqlite3_column_bytes(stmt, 1);
                int stored_dim = sqlite3_column_int(stmt, 2);

                if (id && blob && bytes > 0 && stored_dim == static_cast<int>(dim)) {
                    std::vector<float> s_vec(bytes / sizeof(float));
                    std::memcpy(s_vec.data(), blob, bytes);
                    float sim = vinox::storage::cosine_similarity(q_vec, s_vec);

                    bool found = false;
                    for (auto& c : candidates) {
                        if (c.message_id == id) {
                            c.vector_score = sim;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        HybridCandidate c;
                        c.message_id = id;
                        c.vector_score = sim;
                        candidates.push_back(c);
                    }
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    // 3. Compute hybrid scores
    for (auto& c : candidates) {
        c.hybrid_score = (1.0f - alpha) * c.bm25_score + alpha * c.vector_score;
    }

    // Sort descending by hybrid_score
    std::sort(candidates.begin(), candidates.end(), [](const HybridCandidate& a, const HybridCandidate& b) {
        return a.hybrid_score > b.hybrid_score;
    });

    size_t count = std::min(candidates.size(), max_results);
    auto* mutable_engine = const_cast<vinox_storage_engine*>(engine);

    for (size_t i = 0; i < count; ++i) {
        mutable_engine->string_pool.push_back(candidates[i].message_id);
        results_out[i].message_id = mutable_engine->string_pool.back().c_str();
        results_out[i].bm25_score = candidates[i].bm25_score;
        results_out[i].vector_score = candidates[i].vector_score;
        results_out[i].hybrid_score = candidates[i].hybrid_score;
    }

    if (results_count_out) {
        *results_count_out = count;
    }

    last_error.clear();
    return VINOX_STATUS_OK;
}

void vinox_storage_engine_close(vinox_storage_engine* engine) {
    if (engine) {
        if (engine->db) {
            sqlite3_close(engine->db);
        }
        delete engine;
    }
}

const char* vinox_storage_last_error(void) {
    return last_error.c_str();
}
