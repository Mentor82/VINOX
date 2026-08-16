#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <sqlite3.h>
#include "vinox/storage.h"

namespace vinox::storage {

void l2_normalize(std::vector<float>& vec);
float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);

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

class SqliteVecBackend : public VectorIndexBackend {
public:
    vinox_vector_backend_kind get_kind() const override {
        return VINOX_VECTOR_BACKEND_SQLITE_VEC;
    }

    bool initialize(sqlite3* db, std::string& err_out) override {
        // Create vec0 virtual table for indexed vector retrieval
        const char* sql = "CREATE VIRTUAL TABLE IF NOT EXISTS message_embeddings_vec USING vec0("
                          "message_id text primary key, "
                          "embedding float[1024]"
                          ");";
        char* err_msg = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            err_out = std::string("sqlite-vec vec0 table init fallback: ") + (err_msg ? err_msg : "unknown error");
            if (err_msg) sqlite3_free(err_msg);
            return false;
        }
        return true;
    }

    bool store_embedding(sqlite3* db, const std::string& message_id, const std::vector<float>& vec, std::string& err_out) override {
        // vec0 virtual table compatibility: delete existing entry first to prevent UNIQUE constraint failure on UPSERT
        const char* del_sql = "DELETE FROM message_embeddings_vec WHERE message_id = ?;";
        sqlite3_stmt* del_stmt = nullptr;
        if (sqlite3_prepare_v2(db, del_sql, -1, &del_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(del_stmt, 1, message_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(del_stmt);
            sqlite3_finalize(del_stmt);
        }

        const char* sql = "INSERT INTO message_embeddings_vec (message_id, embedding) VALUES (?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            err_out = sqlite3_errmsg(db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, message_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 2, vec.data(), static_cast<int>(vec.size() * sizeof(float)), SQLITE_STATIC);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            err_out = sqlite3_errmsg(db);
            return false;
        }
        return true;
    }

    bool search_knn(sqlite3* db, const std::vector<float>& query_vec, size_t max_results, std::vector<VectorMatch>& matches_out, std::string& err_out) override {
        const char* sql = "SELECT message_id, distance FROM message_embeddings_vec WHERE embedding MATCH ? AND k = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            err_out = sqlite3_errmsg(db);
            return false;
        }
        sqlite3_bind_blob(stmt, 1, query_vec.data(), static_cast<int>(query_vec.size() * sizeof(float)), SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, static_cast<int>(max_results));

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            double dist = sqlite3_column_double(stmt, 1);
            if (id) {
                VectorMatch m;
                m.message_id = id;
                m.distance = static_cast<float>(dist);
                m.similarity = static_cast<float>(1.0 - (dist / 2.0));
                matches_out.push_back(m);
            }
        }
        sqlite3_finalize(stmt);
        return true;
    }
};

class BruteForceReferenceBackend : public VectorIndexBackend {
public:
    vinox_vector_backend_kind get_kind() const override {
        return VINOX_VECTOR_BACKEND_BRUTE_FORCE_REF;
    }

    bool initialize(sqlite3* db, std::string& err_out) override {
        const char* sql = "CREATE TABLE IF NOT EXISTS message_embeddings_ref ("
                          "message_id TEXT PRIMARY KEY, "
                          "embedding BLOB NOT NULL, "
                          "dim INTEGER NOT NULL, "
                          "created_at_ms INTEGER NOT NULL"
                          ");";
        char* err_msg = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            err_out = std::string("Brute force reference table init failed: ") + (err_msg ? err_msg : "unknown");
            if (err_msg) sqlite3_free(err_msg);
            return false;
        }
        return true;
    }

    bool store_embedding(sqlite3* db, const std::string& message_id, const std::vector<float>& vec, std::string& err_out) override {
        const char* sql = "INSERT OR REPLACE INTO message_embeddings_ref (message_id, embedding, dim, created_at_ms) VALUES (?, ?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            err_out = sqlite3_errmsg(db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, message_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 2, vec.data(), static_cast<int>(vec.size() * sizeof(float)), SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, static_cast<int>(vec.size()));
        sqlite3_bind_int64(stmt, 4, 1000);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            err_out = sqlite3_errmsg(db);
            return false;
        }
        return true;
    }

    bool search_knn(sqlite3* db, const std::vector<float>& query_vec, size_t max_results, std::vector<VectorMatch>& matches_out, std::string& err_out) override {
        const char* sql = "SELECT message_id, embedding, dim FROM message_embeddings_ref;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            err_out = sqlite3_errmsg(db);
            return false;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const void* blob = sqlite3_column_blob(stmt, 1);
            int bytes = sqlite3_column_bytes(stmt, 1);
            int dim = sqlite3_column_int(stmt, 2);

            if (id && blob && bytes > 0 && dim == static_cast<int>(query_vec.size())) {
                std::vector<float> stored_vec(bytes / sizeof(float));
                std::memcpy(stored_vec.data(), blob, bytes);
                float sim = cosine_similarity(query_vec, stored_vec);
                VectorMatch m;
                m.message_id = id;
                m.distance = 1.0f - sim;
                m.similarity = sim;
                matches_out.push_back(m);
            }
        }
        sqlite3_finalize(stmt);

        // Sort descending by similarity
        std::sort(matches_out.begin(), matches_out.end(), [](const VectorMatch& a, const VectorMatch& b) {
            if (std::abs(a.similarity - b.similarity) > 1e-6f) {
                return a.similarity > b.similarity;
            }
            return a.message_id < b.message_id;
        });

        if (max_results > 0 && matches_out.size() > max_results) {
            matches_out.resize(max_results);
        }
        return true;
    }
};

std::unique_ptr<VectorIndexBackend> create_vector_backend(sqlite3* db) {
    auto vec = std::make_unique<SqliteVecBackend>();
    std::string err;
    if (vec->initialize(db, err)) {
        return vec;
    }
    // Fallback to reference backend
    auto ref = std::make_unique<BruteForceReferenceBackend>();
    ref->initialize(db, err);
    return ref;
}

}  // namespace vinox::storage
