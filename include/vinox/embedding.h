#ifndef VINOX_EMBEDDING_H
#define VINOX_EMBEDDING_H

#include <stddef.h>
#include <stdint.h>

#include "vinox/export.h"
#include "vinox/vinox.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pooling strategies for embedding models
typedef enum vinox_embedding_pooling_mode {
    VINOX_EMBEDDING_POOLING_AUTO = 0,        /**< Infer from model config/manifest, fail-closed if unknown */
    VINOX_EMBEDDING_POOLING_MEAN = 1,        /**< Attention-weighted mean pooling over hidden states */
    VINOX_EMBEDDING_POOLING_CLS = 2,         /**< First token / [CLS] hidden state */
    VINOX_EMBEDDING_POOLING_LAST_TOKEN = 3,  /**< Last non-padding token hidden state */
    VINOX_EMBEDDING_POOLING_MODEL_OUTPUT = 4 /**< Model already outputs single pooled vector */
} vinox_embedding_pooling_mode;

// Normalization modes for embedding vectors
typedef enum vinox_embedding_normalization {
    VINOX_EMBEDDING_NORM_AUTO = 0, /**< Infer default for model architecture, fail-closed if unknown */
    VINOX_EMBEDDING_NORM_L2 = 1,   /**< Unit L2 norm (sum(v^2) == 1.0) */
    VINOX_EMBEDDING_NORM_NONE = 2  /**< Raw unnormalized float vectors */
} vinox_embedding_normalization;

// Embedding engine creation options (Prefix-Layout ABI)
typedef struct vinox_embedding_options {
    uint32_t struct_size;           /**< Size of struct for ABI compatibility */
    const char* model_path;         /**< Directory containing embedding model files */
    const char* device;             /**< Target device: "CPU", "GPU", "NPU" (independent from chat LLM device) */
    uint32_t pooling_mode;          /**< vinox_embedding_pooling_mode */
    uint32_t normalization;         /**< vinox_embedding_normalization */
    uint8_t enable_mmap;            /**< Zero-copy virtual memory mapping */
    uint8_t enable_cache;           /**< Model compilation cache directory */
    const char* cache_dir;          /**< Path to compilation blob cache */
} vinox_embedding_options;

#define VINOX_EMBEDDING_OPTIONS_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_embedding_options, normalization) + sizeof(uint32_t)))

// Embedding engine metadata & provenance contract (Prefix-Layout ABI)
typedef struct vinox_embedding_info {
    uint32_t struct_size;
    const char* backend_id;         /**< e.g. "openvino", "onnx", "remote" */
    const char* model_id;           /**< Model name/identifier */
    const char* device;             /**< Assigned device e.g. "NPU", "CPU", "GPU" */
    size_t dimension;               /**< Embedding vector dimensionality (e.g. 1024, 768, 384) */
    uint32_t pooling_mode;          /**< Applied pooling mode */
    uint32_t normalization;         /**< Applied normalization mode */
    const char* space_id;           /**< sha256(weights_hash + tokenizer_hash + pooling + norm + dim) */
} vinox_embedding_info;

#define VINOX_EMBEDDING_INFO_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_embedding_info, space_id) + sizeof(const char*)))

// Opaque embedding engine handle
typedef struct vinox_embedding_engine vinox_embedding_engine;

// Lifecycle
VINOX_API vinox_status vinox_embedding_engine_create(
    const vinox_embedding_options* options,
    vinox_embedding_engine** engine_out
);

VINOX_API vinox_status vinox_embedding_engine_destroy(
    vinox_embedding_engine* engine
);

// Batch-First Inferences
VINOX_API vinox_status vinox_embedding_generate_batch(
    vinox_embedding_engine* engine,
    const char* const* texts,
    size_t count,
    float* vectors_out,
    size_t vector_cap_per_item,
    size_t* dim_out
);

VINOX_API vinox_status vinox_embedding_generate(
    vinox_embedding_engine* engine,
    const char* text,
    float* vector_out,
    size_t vector_cap,
    size_t* dim_out
);

// Queries & Provenance
VINOX_API vinox_status vinox_embedding_get_dim(
    const vinox_embedding_engine* engine,
    size_t* dim_out
);

VINOX_API vinox_status vinox_embedding_get_info(
    const vinox_embedding_engine* engine,
    vinox_embedding_info* info_out,
    char* pool_buf,
    size_t pool_buf_size
);

// Diagnostics
VINOX_API const char* vinox_embedding_last_error(void);

#ifdef __cplusplus
}
#endif

#endif // VINOX_EMBEDDING_H
