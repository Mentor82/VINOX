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

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <iomanip>
#endif

namespace fs = std::filesystem;

namespace {

static std::string calculate_sha256(const std::string& input) {
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    auto right_rotate = [](uint32_t val, uint32_t count) {
        return (val >> count) | (val << (32 - count));
    };

    std::vector<uint8_t> data(input.begin(), input.end());
    uint64_t bit_len = static_cast<uint64_t>(input.size()) * 8;

    data.push_back(0x80);
    while ((data.size() % 64) != 56) {
        data.push_back(0x00);
    }

    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }

    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(data[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(data[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = right_rotate(w[i - 15], 7) ^ right_rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = right_rotate(w[i - 2], 17) ^ right_rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], h_val = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = right_rotate(e, 6) ^ right_rotate(e, 11) ^ right_rotate(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h_val + S1 + ch + k[i] + w[i];
            uint32_t S0 = right_rotate(a, 2) ^ right_rotate(a, 13) ^ right_rotate(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h_val = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;
    }

    std::ostringstream ss;
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << std::setw(8) << std::setfill('0') << h[i];
    }
    return ss.str();
}

} // namespace

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
    std::recursive_mutex mutex;
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

    std::lock_guard<std::recursive_mutex> lock(engine->mutex);

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

    std::lock_guard<std::recursive_mutex> lock(engine->mutex);

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

    std::lock_guard<std::recursive_mutex> lock(const_cast<vinox_storage_engine*>(engine)->mutex);

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

    std::lock_guard<std::recursive_mutex> lock(const_cast<vinox_storage_engine*>(engine)->mutex);

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

    std::lock_guard<std::recursive_mutex> lock(engine->mutex);

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
    float relation_score{0.0f};
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

    std::lock_guard<std::recursive_mutex> lock(const_cast<vinox_storage_engine*>(engine)->mutex);

    std::vector<float> q_vec;
    if (query_embedding && dim > 0) {
        q_vec.assign(query_embedding, query_embedding + dim);
        vinox::storage::l2_normalize(q_vec);
    }

    std::vector<HybridCandidate> candidates;

    // 1. LEXICAL SEARCH VIA REAL FTS5 MATCH & BM25 RANKING SIGNAL
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

    // 3. GRAPH RELATION SIGNAL COMPUTATION (Phase 5.3)
    sqlite3_stmt* rel_stmt = nullptr;
    const char* rel_sql = "SELECT MAX(confidence) FROM typed_relations WHERE source_id = ? OR target_id = ?;";
    if (sqlite3_prepare_v2(engine->db, rel_sql, -1, &rel_stmt, nullptr) == SQLITE_OK) {
        for (auto& c : candidates) {
            sqlite3_bind_text(rel_stmt, 1, c.message_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(rel_stmt, 2, c.message_id.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(rel_stmt) == SQLITE_ROW && sqlite3_column_type(rel_stmt, 0) != SQLITE_NULL) {
                c.relation_score = static_cast<float>(sqlite3_column_double(rel_stmt, 0));
            }
            sqlite3_reset(rel_stmt);
        }
        sqlite3_finalize(rel_stmt);
    }

    // 4. DETERMINISTIC 3-SIGNAL HYBRID FUSION SCORE COMPUTATION & TIE-BREAKING
    for (auto& c : candidates) {
        c.hybrid_score = (1.0f - alpha) * c.bm25_score + alpha * c.vector_score + 0.1f * c.relation_score;
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
    std::lock_guard<std::recursive_mutex> lock(engine->mutex);

    std::string doc_id = generate_uuid();
    std::string content_str(content);
    std::string content_hash = calculate_sha256(content_str);
    uint64_t now = current_timestamp_ms();

    // Atomic transaction for document + all chunks
    sqlite3_exec(engine->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt = nullptr;
    const char* sql_doc = "INSERT INTO documents (id, title, source_uri, content_hash, mime_type, created_at_ms, updated_at_ms) VALUES (?, ?, ?, ?, 'text/plain', ?, ?);";
    if (sqlite3_prepare_v2(engine->db, sql_doc, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
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
        sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return fail_runtime("Failed to insert document");
    }
    sqlite3_finalize(stmt);

    // Multi-chunk splitting logic (~500 chars per chunk)
    std::vector<std::string> chunks_list;
    size_t chunk_size = 500;
    if (content_str.length() <= chunk_size) {
        chunks_list.push_back(content_str);
    } else {
        for (size_t i = 0; i < content_str.length(); i += chunk_size) {
            chunks_list.push_back(content_str.substr(i, chunk_size));
        }
    }

    const char* sql_chunk = "INSERT INTO chunks (id, document_id, chunk_index, content, token_count, created_at_ms) VALUES (?, ?, ?, ?, ?, ?);";
    for (size_t idx = 0; idx < chunks_list.size(); ++idx) {
        std::string chunk_id = generate_uuid();
        sqlite3_stmt* c_stmt = nullptr;
        if (sqlite3_prepare_v2(engine->db, sql_chunk, -1, &c_stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return fail_runtime("Failed to prepare chunk insert");
        }
        sqlite3_bind_text(c_stmt, 1, chunk_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(c_stmt, 2, doc_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(c_stmt, 3, static_cast<int>(idx));
        sqlite3_bind_text(c_stmt, 4, chunks_list[idx].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(c_stmt, 5, static_cast<int>(chunks_list[idx].length() / 4));
        sqlite3_bind_int64(c_stmt, 6, static_cast<sqlite3_int64>(now));

        if (sqlite3_step(c_stmt) != SQLITE_DONE) {
            sqlite3_finalize(c_stmt);
            sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return fail_runtime("Failed to insert chunk");
        }
        sqlite3_finalize(c_stmt);
    }

    if (sqlite3_exec(engine->db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return fail_runtime("Failed to commit document transaction");
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
    if (!engine || !source_id || source_id[0] == '\0' || !target_id || target_id[0] == '\0' || !relation_type || relation_type[0] == '\0') {
        return fail_arg("Invalid or empty required identifiers for relation creation");
    }
    if (confidence < 0.0f || confidence > 1.0f) {
        return fail_arg("Relation confidence out of range [0.0, 1.0]");
    }

    std::lock_guard<std::recursive_mutex> lock(engine->mutex);
    sqlite3_exec(engine->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    std::string rel_id = generate_uuid();
    uint64_t now = current_timestamp_ms();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO typed_relations (id, source_id, target_id, relation_type, evidence_text, confidence, created_at_ms) VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
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
        sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return fail_runtime("Failed to insert relation");
    }
    sqlite3_finalize(stmt);

    if (evidence_text && strlen(evidence_text) > 0) {
        std::string ev_id = generate_uuid();
        const char* sql_ev = "INSERT INTO evidence (id, relation_id, evidence_text, confidence, created_at_ms) VALUES (?, ?, ?, ?, ?);";
        if (sqlite3_prepare_v2(engine->db, sql_ev, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return fail_runtime("Failed to prepare evidence insert");
        }
        sqlite3_bind_text(stmt, 1, ev_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, rel_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, evidence_text, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, static_cast<double>(confidence));
        sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(now));

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return fail_runtime("Failed to insert evidence");
        }
        sqlite3_finalize(stmt);
    }

    if (sqlite3_exec(engine->db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return fail_runtime("Failed to commit relation transaction");
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

VINOX_API vinox_status vinox_storage_store_chunk_embedding(
    vinox_storage_engine* engine,
    const char* chunk_id,
    const float* embedding_data,
    size_t dim
) {
    if (!engine || !chunk_id || chunk_id[0] == '\0' || !embedding_data || dim == 0) {
        return fail_arg("Invalid argument for vinox_storage_store_chunk_embedding");
    }
    std::lock_guard<std::recursive_mutex> lock(engine->mutex);

    // PRE-CHECK DIMENSION AND BACKEND COMPATIBILITY BEFORE ANY SIDE-EFFECTS
    if (engine->vector_backend) {
        if (engine->index_dim != 0 && engine->index_dim != dim) {
            std::string mismatch_err = "Embedding dimension mismatch: query dim (" + std::to_string(dim) + ") != index dim (" + std::to_string(engine->index_dim) + ")";
            return fail_arg(mismatch_err.c_str());
        }
    }

    std::vector<float> vec(embedding_data, embedding_data + dim);
    vinox::storage::l2_normalize(vec);
    uint64_t now = current_timestamp_ms();

    bool manage_tx = (sqlite3_get_autocommit(engine->db) != 0);
    if (manage_tx) {
        sqlite3_exec(engine->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO chunk_embeddings (chunk_id, embedding, dim, created_at_ms) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::string err = std::string("Failed to prepare chunk_embeddings insert: ") + sqlite3_errmsg(engine->db);
        if (manage_tx) sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return fail_runtime(err.c_str());
    }

    sqlite3_bind_text(stmt, 1, chunk_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, vec.data(), static_cast<int>(vec.size() * sizeof(float)), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(dim));
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(now));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        if (manage_tx) sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return fail_runtime("Failed to insert chunk embedding");
    }
    sqlite3_finalize(stmt);

    if (engine->vector_backend) {
        std::string vec_key = "chunk:" + std::string(chunk_id);
        std::string err;
        if (!engine->vector_backend->store_embedding(engine->db, vec_key, vec, err)) {
            if (manage_tx) sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
            std::string fail_msg = "Vector backend store_embedding failed: " + err;
            return fail_runtime(fail_msg.c_str());
        }
    }

    if (manage_tx) {
        if (sqlite3_exec(engine->db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return fail_runtime("Failed to commit chunk embedding transaction");
        }
    }

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status vinox_storage_search_chunks_fts(
    const vinox_storage_engine* engine,
    const char* query,
    size_t max_results,
    size_t* match_count_out
) {
    (void)max_results;
    if (!engine || !query || query[0] == '\0') {
        return fail_arg("Invalid argument for vinox_storage_search_chunks_fts");
    }
    std::lock_guard<std::recursive_mutex> lock(const_cast<vinox_storage_engine*>(engine)->mutex);

    const char* sql = "SELECT COUNT(*) FROM chunks_fts WHERE chunks_fts MATCH ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail_runtime("Failed to prepare chunks_fts query");
    }

    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);

    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<size_t>(sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);

    if (match_count_out) {
        *match_count_out = count;
    }

    return VINOX_STATUS_OK;
}

/* Phase 5.4 — Storage Lifecycle, Online Backup & Portability Implementation */
VINOX_API vinox_status vinox_storage_backup_online(
    vinox_storage_engine* engine,
    const char* backup_db_path
) {
    if (!engine || !backup_db_path) return fail_arg("Invalid argument for backup");
    std::lock_guard<std::recursive_mutex> lock(engine->mutex);

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

    auto export_table = [&](const char* sql, const std::function<nlohmann::json(sqlite3_stmt*)>& mapper) -> nlohmann::json {
        nlohmann::json arr = nlohmann::json::array();
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                arr.push_back(mapper(stmt));
            }
            sqlite3_finalize(stmt);
        }
        return arr;
    };

    root["conversations"] = export_table("SELECT id, title, created_at_ms, updated_at_ms FROM conversations;", [](sqlite3_stmt* stmt) {
        nlohmann::json item;
        item["id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["title"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item["created_at_ms"] = sqlite3_column_int64(stmt, 2);
        item["updated_at_ms"] = sqlite3_column_int64(stmt, 3);
        return item;
    });

    root["messages"] = export_table("SELECT id, conversation_id, parent_id, role, content, provenance_kind, created_at_ms FROM messages;", [](sqlite3_stmt* stmt) {
        nlohmann::json item;
        item["id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["conversation_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (parent) item["parent_id"] = parent; else item["parent_id"] = nullptr;
        item["role"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item["content"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        item["provenance_kind"] = sqlite3_column_int(stmt, 5);
        item["created_at_ms"] = sqlite3_column_int64(stmt, 6);
        return item;
    });

    root["documents"] = export_table("SELECT id, title, source_uri, content_hash, mime_type, created_at_ms, updated_at_ms FROM documents;", [](sqlite3_stmt* stmt) {
        nlohmann::json item;
        item["id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["title"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* uri = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item["source_uri"] = uri ? uri : "";
        item["content_hash"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item["mime_type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        item["created_at_ms"] = sqlite3_column_int64(stmt, 5);
        item["updated_at_ms"] = sqlite3_column_int64(stmt, 6);
        return item;
    });

    root["chunks"] = export_table("SELECT id, document_id, chunk_index, content, token_count, created_at_ms FROM chunks;", [](sqlite3_stmt* stmt) {
        nlohmann::json item;
        item["id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["document_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item["chunk_index"] = sqlite3_column_int(stmt, 2);
        item["content"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item["token_count"] = sqlite3_column_int(stmt, 4);
        item["created_at_ms"] = sqlite3_column_int64(stmt, 5);
        return item;
    });

    root["typed_relations"] = export_table("SELECT id, source_id, target_id, relation_type, evidence_text, confidence, created_at_ms FROM typed_relations;", [](sqlite3_stmt* stmt) {
        nlohmann::json item;
        item["id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["source_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item["target_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item["relation_type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* ev = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        item["evidence_text"] = ev ? ev : "";
        item["confidence"] = sqlite3_column_double(stmt, 5);
        item["created_at_ms"] = sqlite3_column_int64(stmt, 6);
        return item;
    });

    root["evidence"] = export_table("SELECT id, relation_id, evidence_text, confidence, created_at_ms FROM evidence;", [](sqlite3_stmt* stmt) {
        nlohmann::json item;
        item["id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["relation_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item["evidence_text"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item["confidence"] = sqlite3_column_double(stmt, 3);
        item["created_at_ms"] = sqlite3_column_int64(stmt, 4);
        return item;
    });

    root["embedding_profiles"] = export_table("SELECT id, model_name, dimension, metric, created_at_ms FROM embedding_profiles;", [](sqlite3_stmt* stmt) {
        nlohmann::json item;
        item["id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        item["model_name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item["dimension"] = sqlite3_column_int(stmt, 2);
        item["metric"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item["created_at_ms"] = sqlite3_column_int64(stmt, 4);
        return item;
    });

    root["chunk_embeddings"] = export_table("SELECT chunk_id, embedding, dim, created_at_ms FROM chunk_embeddings;", [](sqlite3_stmt* stmt) {
        nlohmann::json item;
        item["chunk_id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const void* blob = sqlite3_column_blob(stmt, 1);
        int blob_bytes = sqlite3_column_bytes(stmt, 1);
        int dim = sqlite3_column_int(stmt, 2);
        item["dim"] = dim;
        item["created_at_ms"] = sqlite3_column_int64(stmt, 3);

        nlohmann::json emb_arr = nlohmann::json::array();
        if (blob && blob_bytes >= static_cast<int>(dim * sizeof(float))) {
            const float* fptr = reinterpret_cast<const float*>(blob);
            for (int i = 0; i < dim; ++i) {
                emb_arr.push_back(fptr[i]);
            }
        }
        item["embedding"] = emb_arr;
        return item;
    });

    std::string dump_str = root.dump();
    if (required_size_out) *required_size_out = dump_str.length() + 1;

    if (!json_out || json_out_size < dump_str.length() + 1) {
        return fail_arg("json_out buffer too small for export payload");
    }

#if defined(_WIN32)
    strncpy_s(json_out, json_out_size, dump_str.c_str(), _TRUNCATE);
#else
    strncpy(json_out, dump_str.c_str(), json_out_size - 1);
    json_out[json_out_size - 1] = '\0';
#endif

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status vinox_storage_import_json(
    vinox_storage_engine* engine,
    const char* json_str
) {
    if (!engine || !json_str) return fail_arg("Invalid argument for import");
    std::lock_guard<std::recursive_mutex> lock(engine->mutex);

    try {
        auto root = nlohmann::json::parse(json_str);
        if (!root.contains("version") || root["version"].get<int>() != 1) {
            return fail_arg("Incompatible or missing JSON version in import payload");
        }

        sqlite3_exec(engine->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        auto step_or_rollback = [&](sqlite3_stmt* stmt, const char* table_name) -> bool {
            int rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                std::string err = std::string("Import row error on table ") + table_name + ": " + sqlite3_errmsg(engine->db);
                sqlite3_finalize(stmt);
                sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
                fail_runtime(err.c_str());
                return false;
            }
            sqlite3_reset(stmt);
            return true;
        };

        // 1. Conversations UPSERT
        if (root.contains("conversations") && root["conversations"].is_array()) {
            const char* sql = "INSERT INTO conversations (id, title, created_at_ms, updated_at_ms) VALUES (?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET title=excluded.title, updated_at_ms=excluded.updated_at_ms;";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
                return fail_runtime(("Failed to prepare conversations import: " + std::string(sqlite3_errmsg(engine->db))).c_str());
            }
            for (const auto& c : root["conversations"]) {
                std::string id = c.value("id", "");
                std::string title = c.value("title", "");
                uint64_t c_at = c.value("created_at_ms", current_timestamp_ms());
                uint64_t u_at = c.value("updated_at_ms", current_timestamp_ms());

                sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(c_at));
                sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(u_at));
                if (!step_or_rollback(stmt, "conversations")) return VINOX_STATUS_RUNTIME_ERROR;
            }
            sqlite3_finalize(stmt);
        }

        // 2. Messages UPSERT
        if (root.contains("messages") && root["messages"].is_array()) {
            const char* sql = "INSERT INTO messages (id, conversation_id, parent_id, role, content, provenance_kind, created_at_ms) VALUES (?, ?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET content=excluded.content, role=excluded.role;";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
                return fail_runtime(("Failed to prepare messages import: " + std::string(sqlite3_errmsg(engine->db))).c_str());
            }
            for (const auto& m : root["messages"]) {
                std::string id = m.value("id", "");
                std::string cid = m.value("conversation_id", "");
                std::string role = m.value("role", "");
                std::string content = m.value("content", "");
                uint32_t prov = m.value("provenance_kind", 0);
                uint64_t c_at = m.value("created_at_ms", current_timestamp_ms());

                sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, cid.c_str(), -1, SQLITE_TRANSIENT);
                if (m.contains("parent_id") && !m["parent_id"].is_null()) {
                    std::string pid = m["parent_id"].get<std::string>();
                    sqlite3_bind_text(stmt, 3, pid.c_str(), -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_bind_null(stmt, 3);
                }
                sqlite3_bind_text(stmt, 4, role.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, content.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 6, static_cast<int>(prov));
                sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(c_at));
                if (!step_or_rollback(stmt, "messages")) return VINOX_STATUS_RUNTIME_ERROR;
            }
            sqlite3_finalize(stmt);
        }

        // 3. Documents UPSERT
        if (root.contains("documents") && root["documents"].is_array()) {
            const char* sql = "INSERT INTO documents (id, title, source_uri, content_hash, mime_type, created_at_ms, updated_at_ms) VALUES (?, ?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET title=excluded.title, updated_at_ms=excluded.updated_at_ms;";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
                return fail_runtime(("Failed to prepare documents import: " + std::string(sqlite3_errmsg(engine->db))).c_str());
            }
            for (const auto& d : root["documents"]) {
                std::string id = d.value("id", "");
                std::string title = d.value("title", "");
                std::string uri = (d.contains("source_uri") && !d["source_uri"].is_null()) ? d["source_uri"].get<std::string>() : "";
                std::string hash = d.value("content_hash", "");
                std::string mime = d.value("mime_type", "text/plain");
                uint64_t c_at = d.value("created_at_ms", current_timestamp_ms());
                uint64_t u_at = d.value("updated_at_ms", current_timestamp_ms());

                sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, uri.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, hash.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, mime.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(c_at));
                sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(u_at));
                if (!step_or_rollback(stmt, "documents")) return VINOX_STATUS_RUNTIME_ERROR;
            }
            sqlite3_finalize(stmt);
        }

        // 4. Chunks UPSERT
        if (root.contains("chunks") && root["chunks"].is_array()) {
            const char* sql = "INSERT INTO chunks (id, document_id, chunk_index, content, token_count, created_at_ms) VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET content=excluded.content;";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
                return fail_runtime(("Failed to prepare chunks import: " + std::string(sqlite3_errmsg(engine->db))).c_str());
            }
            for (const auto& ch : root["chunks"]) {
                std::string id = ch.value("id", "");
                std::string doc_id = ch.value("document_id", "");
                int idx = ch.value("chunk_index", 0);
                std::string content = ch.value("content", "");
                int tokens = ch.value("token_count", 0);
                uint64_t c_at = ch.value("created_at_ms", current_timestamp_ms());

                sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, doc_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 3, idx);
                sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 5, tokens);
                sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(c_at));
                if (!step_or_rollback(stmt, "chunks")) return VINOX_STATUS_RUNTIME_ERROR;
            }
            sqlite3_finalize(stmt);
        }

        // 5. Typed Relations UPSERT
        if (root.contains("typed_relations") && root["typed_relations"].is_array()) {
            const char* sql = "INSERT INTO typed_relations (id, source_id, target_id, relation_type, evidence_text, confidence, created_at_ms) VALUES (?, ?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET confidence=excluded.confidence, evidence_text=excluded.evidence_text;";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
                return fail_runtime(("Failed to prepare typed_relations import: " + std::string(sqlite3_errmsg(engine->db))).c_str());
            }
            for (const auto& r : root["typed_relations"]) {
                std::string id = r.value("id", "");
                std::string src = r.value("source_id", "");
                std::string tgt = r.value("target_id", "");
                std::string rtype = r.value("relation_type", "");
                std::string ev = (r.contains("evidence_text") && !r["evidence_text"].is_null()) ? r["evidence_text"].get<std::string>() : "";
                double conf = r.value("confidence", 1.0);
                uint64_t c_at = r.value("created_at_ms", current_timestamp_ms());

                sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, src.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, tgt.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, rtype.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, ev.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 6, conf);
                sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(c_at));
                if (!step_or_rollback(stmt, "typed_relations")) return VINOX_STATUS_RUNTIME_ERROR;
            }
            sqlite3_finalize(stmt);
        }

        // 6. Evidence UPSERT
        if (root.contains("evidence") && root["evidence"].is_array()) {
            const char* sql = "INSERT INTO evidence (id, relation_id, evidence_text, confidence, created_at_ms) VALUES (?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET confidence=excluded.confidence;";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
                return fail_runtime(("Failed to prepare evidence import: " + std::string(sqlite3_errmsg(engine->db))).c_str());
            }
            for (const auto& ev : root["evidence"]) {
                std::string id = ev.value("id", "");
                std::string rel_id = ev.value("relation_id", "");
                std::string text = ev.value("evidence_text", "");
                double conf = ev.value("confidence", 1.0);
                uint64_t c_at = ev.value("created_at_ms", current_timestamp_ms());

                sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, rel_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, text.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 4, conf);
                sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(c_at));
                if (!step_or_rollback(stmt, "evidence")) return VINOX_STATUS_RUNTIME_ERROR;
            }
            sqlite3_finalize(stmt);
        }

        // 7. Chunk Embeddings UPSERT & Vector Index Re-materialization
        if (root.contains("chunk_embeddings") && root["chunk_embeddings"].is_array()) {
            for (const auto& item : root["chunk_embeddings"]) {
                std::string chunk_id = item.value("chunk_id", "");
                size_t dim = item.value("dim", 0);
                if (chunk_id.empty() || dim == 0 || !item.contains("embedding") || !item["embedding"].is_array()) continue;

                std::vector<float> vec;
                vec.reserve(dim);
                for (const auto& val : item["embedding"]) {
                    vec.push_back(val.get<float>());
                }
                if (vec.size() == dim) {
                    vinox_status st = vinox_storage_store_chunk_embedding(engine, chunk_id.c_str(), vec.data(), dim);
                    if (st != VINOX_STATUS_OK) {
                        sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
                        std::string err_msg = std::string("Failed to import chunk embedding: ") + vinox_last_error();
                        return fail_runtime(err_msg.c_str());
                    }
                }
            }
        }

        if (sqlite3_exec(engine->db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return fail_runtime("Failed to commit JSON import transaction");
        }

        return VINOX_STATUS_OK;
    } catch (...) {
        sqlite3_exec(engine->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return fail_runtime("JSON import parse or execution error");
    }
}

const char* vinox_storage_last_error(void) {
    return vinox_last_error();
}

