#ifndef VINOX_STORAGE_H
#define VINOX_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "vinox/export.h"
#include "vinox/vinox.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vinox_storage_engine vinox_storage_engine;

/**
 * @brief Conversation metadata structure.
 * @note Pointer lifetime contract: C-string pointers (id, title) populated by the API
 * remain valid for the lifetime of the underlying vinox_storage_engine instance.
 */
typedef struct vinox_conversation_info {
    uint32_t struct_size;
    const char* id;
    const char* title;
    uint64_t created_at_ms;
    uint64_t updated_at_ms;
} vinox_conversation_info;

#define VINOX_CONVERSATION_INFO_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_conversation_info, title) + sizeof(const char*)))

/**
 * @brief Message item structure.
 * @note Pointer lifetime contract: C-string pointers (id, conversation_id, parent_id, role, content)
 * populated in message_out remain valid for the lifetime of the underlying vinox_storage_engine instance.
 */
typedef struct vinox_message_info {
    uint32_t struct_size;
    const char* id;
    const char* conversation_id;
    const char* parent_id;
    const char* role;
    const char* content;
    uint32_t provenance_kind; /* vinox_provenance_kind */
    uint64_t created_at_ms;
} vinox_message_info;

#define VINOX_MESSAGE_INFO_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_message_info, content) + sizeof(const char*)))

/**
 * @brief Hybrid search result item.
 */
typedef struct vinox_search_result {
    uint32_t struct_size;
    const char* message_id;
    float bm25_score;
    float vector_score;
    float hybrid_score;
} vinox_search_result;

#define VINOX_SEARCH_RESULT_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_search_result, vector_score) + sizeof(float)))

typedef enum vinox_vector_backend_kind {
    VINOX_VECTOR_BACKEND_SQLITE_VEC = 1,
    VINOX_VECTOR_BACKEND_BRUTE_FORCE_REF = 2
} vinox_vector_backend_kind;

VINOX_API vinox_status vinox_storage_engine_open(
    const char* db_path,
    vinox_storage_engine** engine_out
);

VINOX_API vinox_status vinox_storage_get_vector_backend_kind(
    const vinox_storage_engine* engine,
    uint32_t* backend_kind_out
);

VINOX_API vinox_status vinox_storage_create_conversation(
    vinox_storage_engine* engine,
    const char* title,
    vinox_conversation_info* info_out
);

#include "vinox/logging.h"

VINOX_API vinox_status vinox_storage_add_message(
    vinox_storage_engine* engine,
    const vinox_message_info* message_in,
    vinox_message_info* message_out
);

VINOX_API vinox_status vinox_storage_add_message_ex(
    vinox_storage_engine* engine,
    const vinox_message_info* message_in,
    const vinox_correlation_context* correlation,
    vinox_message_info* message_out
);

VINOX_API vinox_status vinox_storage_get_conversation_count(
    const vinox_storage_engine* engine,
    size_t* count_out
);

VINOX_API vinox_status vinox_storage_search_messages_fts(
    const vinox_storage_engine* engine,
    const char* query,
    size_t max_results,
    size_t* match_count_out
);

VINOX_API vinox_status vinox_storage_store_embedding(
    vinox_storage_engine* engine,
    const char* message_id,
    const float* embedding_data,
    size_t dim
);

VINOX_API vinox_status vinox_storage_search_hybrid(
    const vinox_storage_engine* engine,
    const float* query_embedding,
    size_t dim,
    const char* text_query,
    float alpha,
    size_t max_results,
    vinox_search_result* results_out,
    size_t* results_count_out
);

VINOX_API void vinox_storage_engine_close(vinox_storage_engine* engine);

VINOX_API const char* vinox_storage_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
