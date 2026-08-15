#include "vinox/storage.h"

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

namespace fs = std::filesystem;

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

const char* INIT_SQL = R"(
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS conversations (
    id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL,
    metadata_json TEXT DEFAULT '{}'
);

CREATE TABLE IF NOT EXISTS messages (
    id TEXT PRIMARY KEY,
    conversation_id TEXT NOT NULL,
    parent_id TEXT,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    provenance_kind INTEGER NOT NULL DEFAULT 0,
    created_at_ms INTEGER NOT NULL,
    FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE,
    FOREIGN KEY(parent_id) REFERENCES messages(id) ON DELETE SET NULL
);

CREATE TABLE IF NOT EXISTS typed_relations (
    id TEXT PRIMARY KEY,
    source_id TEXT NOT NULL,
    target_id TEXT NOT NULL,
    relation_type TEXT NOT NULL,
    evidence_text TEXT,
    confidence REAL NOT NULL DEFAULT 1.0,
    created_at_ms INTEGER NOT NULL
);
)";

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

    // Enable WAL mode
    char* err_msg = nullptr;
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    // Run schema migrations
    rc = sqlite3_exec(db, INIT_SQL, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "Failed to execute schema init SQL";
        if (err_msg) sqlite3_free(err_msg);
        sqlite3_close(db);
        return fail_runtime(err.c_str());
    }

    // Try creating FTS5 table if supported
    sqlite3_exec(db, "CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5(content);", nullptr, nullptr, nullptr);

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
    if (message_in->struct_size < VINOX_MESSAGE_INFO_MIN_SIZE) {
        return fail_abi("message_in->struct_size is smaller than VINOX_MESSAGE_INFO_MIN_SIZE");
    }
    if (message_in->conversation_id == nullptr || message_in->content == nullptr || message_in->role == nullptr) {
        return fail_arg("message required fields (conversation_id, role, content) cannot be null");
    }

    std::lock_guard<std::mutex> lock(engine->mutex);

    MessageEntry entry;
    entry.id = message_in->id ? message_in->id : generate_uuid();
    entry.conversation_id = message_in->conversation_id;
    entry.parent_id = message_in->parent_id ? message_in->parent_id : "";
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
        if (message_out->struct_size < VINOX_MESSAGE_INFO_MIN_SIZE) {
            return fail_abi("message_out->struct_size is smaller than VINOX_MESSAGE_INFO_MIN_SIZE");
        }
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

    std::string pattern = "%" + std::string(query) + "%";
    const char* sql = "SELECT COUNT(*) FROM messages WHERE content LIKE ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(engine->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return fail_runtime(sqlite3_errmsg(engine->db));
    }

    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_STATIC);

    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);

    if (match_count_out) {
        *match_count_out = (max_results > 0 && count > max_results) ? max_results : count;
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
