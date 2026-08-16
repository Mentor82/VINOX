#include "vinox/logging.h"
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

#ifndef SQLITE_CORE
#define SQLITE_CORE
#endif
#include <sqlite3.h>

#include "generated/001_init_sql.h"
#include "generated/002_documents_relations_sql.h"
#include <nlohmann/json.hpp>
#include "sqlite-vec/sqlite-vec.h"

namespace fs = std::filesystem;

namespace vinox::storage {
void l2_normalize(std::vector<float>& vec);
float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);
float sigmoid_normalize_bm25(float raw_bm25);
float normalize_cosine_similarity(float sim);

struct VectorMatch {
    std::string message_id;
    float distance{0.0f};
    float similarity{0.0f};
};

class VectorIndexBackend {
public:
    virtual ~VectorIndexBackend() = default;
    virtual vinox_vector_backend_kind get_kind() const = 0;
    virtual bool initialize(sqlite3* db, std::string& err_out) = 0;
    virtual bool store_embedding(sqlite3* db, const std::string& message_id, const std::vector<float>& vec, std::string& err_out) = 0;
    virtual bool search_knn(sqlite3* db, const std::vector<float>& query_vec, size_t max_results, std::vector<VectorMatch>& matches_out, std::string& err_out) = 0;
};

std::unique_ptr<VectorIndexBackend> create_vector_backend(sqlite3* db);
}  // namespace vinox::storage

vinox_status fail_arg(const char* message) {
    vinox_set_last_error(message);
    return VINOX_STATUS_INVALID_ARGUMENT;
}

vinox_status fail_abi(const char* message) {
    vinox_set_last_error(message);
    return VINOX_STATUS_INCOMPATIBLE_ABI;
}

vinox_status fail_runtime(const char* message) {
    vinox_set_last_error(message);
    return VINOX_STATUS_RUNTIME_ERROR;
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
    std::unique_ptr<vinox::storage::VectorIndexBackend> vector_backend;
    size_t index_dim{1024};
};

namespace {

#define VINOX_FIELD_PRESENT(ptr, member) \
    ((ptr)->struct_size >= (offsetof(std::remove_pointer_t<decltype(ptr)>, member) + sizeof((ptr)->member)))

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

    // INITIALIZE SQLITE-VEC EXTENSION DIRECTLY ON THE CONNECTION
    if (sqlite3_vec_init(db, &err_msg, nullptr) != SQLITE_OK) {
        err_out = std::string("Failed to initialize sqlite-vec extension: ") + (err_msg ? err_msg : "unknown error");
        if (err_msg) sqlite3_free(err_msg);
        return false;
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

    /* Migration 002: Documents, Chunks, Typed Relations & Evidence */
    stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM schema_migrations WHERE version = 2;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        err_out = std::string("Failed to check migration 002 version: ") + sqlite3_errmsg(db);
        return false;
    }

    bool m2_applied = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        m2_applied = (sqlite3_column_int(stmt, 0) > 0);
    }
    sqlite3_finalize(stmt);

    if (!m2_applied) {
        rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            err_out = std::string("Failed to begin migration 002 transaction: ") + (err_msg ? err_msg : "unknown error");
            if (err_msg) sqlite3_free(err_msg);
            return false;
        }

        rc = sqlite3_exec(db, vinox::storage::CANONICAL_MIGRATION_002_SQL, nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            err_out = std::string("Canonical migration 002 failed: ") + (err_msg ? err_msg : "unknown error");
            if (err_msg) sqlite3_free(err_msg);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        std::string record_sql = "INSERT INTO schema_migrations (version, applied_at_ms) VALUES (2, " + std::to_string(current_timestamp_ms()) + ");";
        rc = sqlite3_exec(db, record_sql.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            err_out = std::string("Failed to record migration 002: ") + (err_msg ? err_msg : "unknown error");
            if (err_msg) sqlite3_free(err_msg);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            err_out = std::string("Failed to commit migration 002: ") + (err_msg ? err_msg : "unknown error");
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
        engine->vector_backend = vinox::storage::create_vector_backend(db);
        *engine_out = engine;
        vinox_set_last_error(nullptr);
        return VINOX_STATUS_OK;
    } catch (...) {
        sqlite3_close(db);
        return fail_runtime("Memory allocation error for storage engine");
    }
}

vinox_status vinox_storage_get_vector_backend_kind(
    const vinox_storage_engine* engine,
    uint32_t* backend_kind_out
) {
    if (engine == nullptr || engine->vector_backend == nullptr) {
        return fail_arg("storage engine handle cannot be null");
    }
    if (backend_kind_out == nullptr) {
        return fail_arg("backend_kind_out pointer cannot be null");
    }
    *backend_kind_out = static_cast<uint32_t>(engine->vector_backend->get_kind());
    vinox_set_last_error(nullptr);
    return VINOX_STATUS_OK;
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

    vinox_set_last_error(nullptr);
    return VINOX_STATUS_OK;
}

vinox_status vinox_storage_add_message(
    vinox_storage_engine* engine,
    const vinox_message_info* message_in,
    vinox_message_info* message_out
) {
    return vinox_storage_add_message_ex(engine, message_in, nullptr, message_out);
}

vinox_status vinox_storage_add_message_ex(
    vinox_storage_engine* engine,
    const vinox_message_info* message_in,
    const vinox_correlation_context* correlation,
    vinox_message_info* message_out
) {
    if (engine == nullptr || engine->db == nullptr) {
        return fail_arg("storage engine handle cannot be null");
    }
    if (message_in == nullptr) {
        return fail_arg("message_in pointer cannot be null");
    }

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

    // EMIT CROSS-DLL OPERATIONAL LOG EVENT WITH CORRELATION PROPAGATION
    vinox_log_event_meta meta{};
    meta.struct_size = sizeof(vinox_log_event_meta);
    meta.details = "Message inserted into SQLite storage engine";
    meta.status = "OK";
    vinox_log_event_ex(VINOX_LOG_INFO, "storage", "message.add", correlation, &meta);

    vinox_set_last_error(nullptr);
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
    vinox_set_last_error(nullptr);
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

    vinox_set_last_error(nullptr);
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

    // FAIL-CLOSED BACKEND INTEGRITY: FAIL IMMEDIATELY IF VECTOR BACKEND WRITING FAILS
    std::string err;
    if (!engine->vector_backend || !engine->vector_backend->store_embedding(engine->db, message_id, vec, err)) {
        std::string fail_msg = "Vector backend store_embedding failed: " + (err.empty() ? "unknown error" : err);
        return fail_runtime(fail_msg.c_str());
    }

    engine->index_dim = dim;
    vinox_set_last_error(nullptr);
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

    // STRICT ALPHA RANGE VALIDATION [0.0, 1.0]
    if (alpha < 0.0f || alpha > 1.0f) {
        return fail_arg("alpha must be in range [0.0, 1.0]");
    }

    // PRE-TRANSACTION ABI VALIDATION ON RESULTS_OUT STRUCTS
    if (results_out != nullptr) {
        for (size_t i = 0; i < max_results; ++i) {
            if (results_out[i].struct_size < VINOX_SEARCH_RESULT_MIN_SIZE) {
                return fail_abi("results_out struct_size is smaller than VINOX_SEARCH_RESULT_MIN_SIZE");
            }
        }
    }

    // EMBEDDING DIMENSION MISMATCH CHECK
    if (query_embedding && dim > 0) {
        if (dim != engine->index_dim) {
            std::string err = "Embedding dimension mismatch: query dim (" + std::to_string(dim) + ") != index dim (" + std::to_string(engine->index_dim) + ")";
            return fail_arg(err.c_str());
        }
    }

    std::lock_guard<std::mutex> lock(const_cast<vinox_storage_engine*>(engine)->mutex);

    std::vector<float> q_vec;
    if (query_embedding && dim > 0) {
        q_vec.assign(query_embedding, query_embedding + dim);
        vinox::storage::l2_normalize(q_vec);
    }

    std::vector<HybridCandidate> candidates;

    // 1. REAL FTS5 BM25 RANKING QUERY (bm25(messages_fts))
    if (text_query && text_query[0] != '\0') {
        const char* sql = "SELECT m.id, bm25(messages_fts) AS raw_bm25 "
                          "FROM messages_fts f "
                          "JOIN messages m ON f.rowid = m.rowid "
                          "WHERE messages_fts MATCH ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, text_query, -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                double raw_bm25 = sqlite3_column_double(stmt, 1);
                if (id) {
                    HybridCandidate c;
                    c.message_id = id;
                    c.bm25_score = vinox::storage::sigmoid_normalize_bm25(static_cast<float>(raw_bm25));
                    candidates.push_back(c);
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    // 2. VECTOR SEARCH VIA ACTIVE VECTOR BACKEND (sqlite-vec / vec0) - FAIL CLOSED ON BACKEND ERROR
    if (!q_vec.empty()) {
        std::vector<vinox::storage::VectorMatch> vec_matches;
        std::string vec_err;
        if (!engine->vector_backend || !engine->vector_backend->search_knn(engine->db, q_vec, max_results, vec_matches, vec_err)) {
            std::string fail_msg = "Vector backend search_knn failed: " + (vec_err.empty() ? "unknown error" : vec_err);
            return fail_runtime(fail_msg.c_str());
        }

        for (const auto& vm : vec_matches) {
            float norm_sim = vinox::storage::normalize_cosine_similarity(vm.similarity);
            bool found = false;
            for (auto& c : candidates) {
                if (c.message_id == vm.message_id) {
                    c.vector_score = norm_sim;
                    found = true;
                    break;
                }
            }
            if (!found) {
                HybridCandidate c;
                c.message_id = vm.message_id;
                c.vector_score = norm_sim;
                candidates.push_back(c);
            }
        }
    }

    // 3. DETERMINISTIC HYBRID FUSION SCORE COMPUTATION & TIE-BREAKING
    for (auto& c : candidates) {
        c.hybrid_score = (1.0f - alpha) * c.bm25_score + alpha * c.vector_score;
    }

    // Sort descending by hybrid_score, secondary tie-break ascending by message_id
    std::sort(candidates.begin(), candidates.end(), [](const HybridCandidate& a, const HybridCandidate& b) {
        if (std::abs(a.hybrid_score - b.hybrid_score) > 1e-6f) {
            return a.hybrid_score > b.hybrid_score;
        }
        return a.message_id < b.message_id;
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

    vinox_set_last_error(nullptr);
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

/* Phase 5.3 — Documents, Typed Relations & Graph CTE Implementation */
VINOX_API vinox_status vinox_storage_document_ingest(
    vinox_storage_engine* engine,
    const char* title,
    const char* content,
    char* doc_id_out,
    size_t doc_id_out_size
) {
    if (!engine || !title || !content || !doc_id_out || doc_id_out_size < 33) {
        return fail_arg("Invalid argument for vinox_storage_document_ingest");
    }
    std::lock_guard<std::mutex> lock(engine->mutex);

    std::string doc_id = generate_uuid();
    std::string content_str(content);
    std::string content_hash = std::to_string(std::hash<std::string>{}(content_str));
    uint64_t now = current_timestamp_ms();

    sqlite3_stmt* stmt = nullptr;
    const char* sql_doc = "INSERT INTO documents (id, title, source_uri, content_hash, mime_type, created_at_ms, updated_at_ms) VALUES (?, ?, ?, ?, 'text/plain', ?, ?);";
    if (sqlite3_prepare_v2(engine->db, sql_doc, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail_runtime("Failed to prepare document insert");
    }

    sqlite3_bind_text(stmt, 1, doc_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, "file://internal", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, content_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(now));
    sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(now));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return fail_runtime("Failed to insert document");
    }
    sqlite3_finalize(stmt);

    /* Split document into chunks and insert into chunks + chunks_fts */
    std::string chunk_id = generate_uuid();
    const char* sql_chunk = "INSERT INTO chunks (id, document_id, chunk_index, content, token_count, created_at_ms) VALUES (?, ?, 0, ?, ?, ?);";
    if (sqlite3_prepare_v2(engine->db, sql_chunk, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, chunk_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, doc_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, static_cast<int>(content_str.length() / 4));
        sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(now));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

#if defined(_WIN32)
    strncpy_s(doc_id_out, doc_id_out_size, doc_id.c_str(), _TRUNCATE);
#else
    strncpy(doc_id_out, doc_id.c_str(), doc_id_out_size - 1);
    doc_id_out[doc_id_out_size - 1] = '\0';
#endif

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status vinox_storage_relation_create(
    vinox_storage_engine* engine,
    const char* source_id,
    const char* target_id,
    const char* relation_type,
    const char* evidence_text,
    float confidence
) {
    if (!engine || !source_id || !target_id || !relation_type) {
        return fail_arg("Invalid argument for vinox_storage_relation_create");
    }
    std::lock_guard<std::mutex> lock(engine->mutex);

    std::string rel_id = generate_uuid();
    uint64_t now = current_timestamp_ms();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO typed_relations (id, source_id, target_id, relation_type, evidence_text, confidence, created_at_ms) VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail_runtime("Failed to prepare relation insert");
    }

    sqlite3_bind_text(stmt, 1, rel_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, source_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, target_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, relation_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, evidence_text ? evidence_text : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, static_cast<double>(confidence));
    sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(now));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return fail_runtime("Failed to insert relation");
    }
    sqlite3_finalize(stmt);

    if (evidence_text && strlen(evidence_text) > 0) {
        std::string ev_id = generate_uuid();
        const char* sql_ev = "INSERT INTO evidence (id, relation_id, evidence_text, confidence, created_at_ms) VALUES (?, ?, ?, ?, ?);";
        if (sqlite3_prepare_v2(engine->db, sql_ev, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, ev_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, rel_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, evidence_text, -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(stmt, 4, static_cast<double>(confidence));
            sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(now));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status vinox_storage_relations_query_cte(
    const vinox_storage_engine* engine,
    const char* entity_id,
    char* json_out,
    size_t json_out_size
) {
    if (!engine || !entity_id || !json_out || json_out_size < 2) {
        return fail_arg("Invalid argument for vinox_storage_relations_query_cte");
    }

    const char* cte_sql =
        "WITH RECURSIVE graph(id, target_id, relation_type, depth) AS ("
        "  SELECT source_id, target_id, relation_type, 1 FROM typed_relations WHERE source_id = ?"
        "  UNION ALL "
        "  SELECT r.source_id, r.target_id, r.relation_type, g.depth + 1 "
        "  FROM typed_relations r JOIN graph g ON r.source_id = g.target_id "
        "  WHERE g.depth < 3"
        ") SELECT id, target_id, relation_type, depth FROM graph;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(engine->db, cte_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail_runtime("Failed to prepare recursive CTE query");
    }

    sqlite3_bind_text(stmt, 1, entity_id, -1, SQLITE_TRANSIENT);

    nlohmann::json rels = nlohmann::json::array();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json item;
        item["source_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["target_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item["relation_type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item["depth"] = sqlite3_column_int(stmt, 3);
        rels.push_back(item);
    }
    sqlite3_finalize(stmt);

    std::string res = rels.dump();
#if defined(_WIN32)
    strncpy_s(json_out, json_out_size, res.c_str(), _TRUNCATE);
#else
    strncpy(json_out, res.c_str(), json_out_size - 1);
    json_out[json_out_size - 1] = '\0';
#endif

    return VINOX_STATUS_OK;
}

/* Phase 5.4 — Storage Lifecycle, Online Backup & Portability Implementation */
VINOX_API vinox_status vinox_storage_backup_online(
    vinox_storage_engine* engine,
    const char* backup_db_path
) {
    if (!engine || !backup_db_path) return fail_arg("Invalid argument for backup");
    std::lock_guard<std::mutex> lock(engine->mutex);

    sqlite3* target_db = nullptr;
    if (sqlite3_open(backup_db_path, &target_db) != SQLITE_OK) {
        if (target_db) sqlite3_close(target_db);
        return fail_runtime("Failed to open target database for backup");
    }

    sqlite3_backup* backup = sqlite3_backup_init(target_db, "main", engine->db, "main");
    if (!backup) {
        sqlite3_close(target_db);
        return fail_runtime("Failed to initialize SQLite online backup");
    }

    int rc = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    sqlite3_close(target_db);

    if (rc != SQLITE_DONE) {
        return fail_runtime("Backup step failed to complete cleanly");
    }

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status vinox_storage_export_json(
    const vinox_storage_engine* engine,
    char* json_out,
    size_t json_out_size,
    size_t* required_size_out
) {
    if (!engine) return fail_arg("Invalid engine for export");

    nlohmann::json root;
    root["version"] = 1;

    /* Export conversations */
    nlohmann::json convs = nlohmann::json::array();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(engine->db, "SELECT id, title, created_at_ms, updated_at_ms FROM conversations;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            nlohmann::json c;
            c["id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            c["title"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            c["created_at_ms"] = sqlite3_column_int64(stmt, 2);
            c["updated_at_ms"] = sqlite3_column_int64(stmt, 3);
            convs.push_back(c);
        }
        sqlite3_finalize(stmt);
    }
    root["conversations"] = convs;

    std::string dump_str = root.dump();
    if (required_size_out) *required_size_out = dump_str.length() + 1;

    if (json_out && json_out_size >= dump_str.length() + 1) {
#if defined(_WIN32)
        strncpy_s(json_out, json_out_size, dump_str.c_str(), _TRUNCATE);
#else
        strncpy(json_out, dump_str.c_str(), json_out_size - 1);
        json_out[json_out_size - 1] = '\0';
#endif
    }

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status vinox_storage_import_json(
    vinox_storage_engine* engine,
    const char* json_str
) {
    if (!engine || !json_str) return fail_arg("Invalid argument for import");
    std::lock_guard<std::mutex> lock(engine->mutex);

    try {
        auto root = nlohmann::json::parse(json_str);
        if (!root.contains("conversations") || !root["conversations"].is_array()) {
            return fail_arg("Invalid JSON structure for import");
        }

        char* err_msg = nullptr;
        sqlite3_exec(engine->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        const char* sql = "INSERT OR REPLACE INTO conversations (id, title, created_at_ms, updated_at_ms) VALUES (?, ?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            for (const auto& c : root["conversations"]) {
                std::string id = c["id"].get<std::string>();
                std::string title = c["title"].get<std::string>();
                uint64_t c_at = c.value("created_at_ms", current_timestamp_ms());
                uint64_t u_at = c.value("updated_at_ms", current_timestamp_ms());

                sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(c_at));
                sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(u_at));
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_exec(engine->db, "COMMIT;", nullptr, nullptr, nullptr);
        return VINOX_STATUS_OK;
    } catch (...) {
        sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return fail_runtime("JSON import parse or execution error");
    }
}

const char* vinox_storage_last_error(void) {
    return vinox_last_error();
}

