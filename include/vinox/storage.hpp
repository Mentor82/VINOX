#ifndef VINOX_STORAGE_HPP
#define VINOX_STORAGE_HPP

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "vinox/storage.h"

namespace vinox::storage {

struct Conversation {
    std::string id;
    std::string title;
    uint64_t created_at_ms{0};
    uint64_t updated_at_ms{0};
};

struct Message {
    std::string id;
    std::string conversation_id;
    std::string parent_id;
    std::string role;
    std::string content;
    vinox_provenance_kind provenance_kind{VINOX_PROVENANCE_SOURCE_LITERAL};
    uint64_t created_at_ms{0};
};

class StorageEngine {
public:
    explicit StorageEngine(std::string_view db_path) {
        vinox_storage_engine* handle = nullptr;
        if (vinox_storage_engine_open(db_path.data(), &handle) != VINOX_STATUS_OK || !handle) {
            throw std::runtime_error(vinox_storage_last_error());
        }
        engine_.reset(handle);
    }

    Conversation create_conversation(std::string_view title) {
        vinox_conversation_info info{};
        info.struct_size = sizeof(info);
        if (vinox_storage_create_conversation(engine_.get(), title.data(), &info) != VINOX_STATUS_OK) {
            throw std::runtime_error(vinox_storage_last_error());
        }
        return {
            info.id ? info.id : "",
            info.title ? info.title : "",
            info.created_at_ms,
            info.updated_at_ms
        };
    }

    size_t get_conversation_count() const {
        size_t count = 0;
        if (vinox_storage_get_conversation_count(engine_.get(), &count) != VINOX_STATUS_OK) {
            throw std::runtime_error(vinox_storage_last_error());
        }
        return count;
    }

    size_t search_messages_fts(std::string_view query, size_t max_results = 50) const {
        size_t matches = 0;
        if (vinox_storage_search_messages_fts(engine_.get(), query.data(), max_results, &matches) != VINOX_STATUS_OK) {
            throw std::runtime_error(vinox_storage_last_error());
        }
        return matches;
    }

private:
    struct EngineDeleter {
        void operator()(vinox_storage_engine* handle) const noexcept {
            if (handle) {
                vinox_storage_engine_close(handle);
            }
        }
    };

    std::unique_ptr<vinox_storage_engine, EngineDeleter> engine_;
};

}  // namespace vinox::storage

#endif
