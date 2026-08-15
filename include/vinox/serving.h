#ifndef VINOX_SERVING_H
#define VINOX_SERVING_H

#include <stddef.h>
#include <stdint.h>

#include "vinox/export.h"
#include "vinox/vinox.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vinox_model_registry vinox_model_registry;

typedef enum vinox_model_state {
    VINOX_MODEL_STATE_UNLOADED = 0,
    VINOX_MODEL_STATE_LOADING  = 1,
    VINOX_MODEL_STATE_READY    = 2,
    VINOX_MODEL_STATE_DRAINING = 3,
    VINOX_MODEL_STATE_ERROR    = 4
} vinox_model_state;

typedef struct vinox_model_info {
    uint32_t struct_size;
    const char* model_id;
    const char* display_name;
    const char* local_path;
    const char* default_device;
    uint64_t context_length;
    uint32_t state;
} vinox_model_info;

/* Minimum required struct_size for backward compatibility (up to local_path) */
#define VINOX_MODEL_INFO_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_model_info, local_path) + sizeof(const char*)))

VINOX_API vinox_status vinox_model_registry_create(vinox_model_registry** registry);

VINOX_API vinox_status vinox_model_registry_scan(
    vinox_model_registry* registry,
    const char* directory_path,
    size_t* count_out
);

VINOX_API vinox_status vinox_model_registry_register_manifest(
    vinox_model_registry* registry,
    const char* manifest_json_path
);

VINOX_API vinox_status vinox_model_registry_get_count(
    const vinox_model_registry* registry,
    size_t* count_out
);

/**
 * @brief Populates `info_out` with metadata for model at `index`.
 *
 * @note Lifetime Contract:
 * Pointers returned in `info_out` (model_id, display_name, local_path, default_device)
 * remain valid for the lifetime of the `vinox_model_registry` instance until `vinox_model_registry_destroy` is called,
 * even across concurrent scans or new model registrations.
 */
VINOX_API vinox_status vinox_model_registry_get_info(
    const vinox_model_registry* registry,
    size_t index,
    vinox_model_info* info_out
);

VINOX_API void vinox_model_registry_destroy(vinox_model_registry* registry);

VINOX_API const char* vinox_serving_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
