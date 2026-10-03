#ifndef VINOX_PLUGINS_H
#define VINOX_PLUGINS_H

#include <stddef.h>
#include <stdint.h>

#include "vinox/export.h"
#include "vinox/vinox.h"
#include "vinox/tools.h"
#include "vinox/storage.h"
#include "vinox/embedding.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VINOX_PLUGIN_ABI_VERSION_1 1

// Standard capability scopes for plugins and tools
typedef enum vinox_plugin_scope {
    VINOX_PLUGIN_SCOPE_NONE = 0,
    VINOX_PLUGIN_SCOPE_FS_READ = 1 << 0,
    VINOX_PLUGIN_SCOPE_FS_WRITE = 1 << 1,
    VINOX_PLUGIN_SCOPE_RETRIEVAL_SEARCH = 1 << 2,
    VINOX_PLUGIN_SCOPE_RETRIEVAL_MUTATE = 1 << 3,
    VINOX_PLUGIN_SCOPE_MATH = 1 << 4,
    VINOX_PLUGIN_SCOPE_SYSTEM_TIME = 1 << 5,
    VINOX_PLUGIN_SCOPE_NETWORK = 1 << 6
} vinox_plugin_scope;

// Forward declaration of plugin struct
typedef struct vinox_tool_plugin vinox_tool_plugin;

// Standard exported dynamic plugin entry point signature
typedef vinox_status (*vinox_plugin_entry_fn)(vinox_tool_plugin* plugin_out);
#define VINOX_PLUGIN_ENTRY_SYMBOL "vinox_plugin_get_api_v1"

// Versioned Plugin Struct (Prefix-Layout ABI)
struct vinox_tool_plugin {
    uint32_t struct_size;                           /**< sizeof(vinox_tool_plugin) */
    uint32_t abi_version;                           /**< VINOX_PLUGIN_ABI_VERSION_1 */
    const char* name;                               /**< Unique plugin identifier e.g. "std_fs" */
    const char* version;                            /**< SemVer string e.g. "1.0.0" */
    const char* description;                        /**< Human-readable description */
    uint32_t declared_scopes;                       /**< Bitmask of vinox_plugin_scope */
    
    // Lifecycle Hooks
    vinox_status (*init)(vinox_tool_plugin* self, void* context);
    vinox_status (*register_tools)(vinox_tool_plugin* self, vinox_tool_registry* registry);
    void (*destroy)(vinox_tool_plugin* self);

    void* user_data;                                /**< Internal plugin state / instance data */
};

#define VINOX_TOOL_PLUGIN_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_tool_plugin, user_data) + sizeof(void*)))

// Plugin Registration & Lifecycle
VINOX_API vinox_status vinox_plugin_register(
    vinox_tool_registry* registry,
    vinox_tool_plugin* plugin
);

VINOX_API void vinox_plugin_destroy(
    vinox_tool_plugin* plugin
);

// Built-in Standard Plugins (Dependency Injection)
VINOX_API vinox_status vinox_plugin_std_fs_create(
    const char* workspace_root,
    vinox_tool_plugin** plugin_out
);

VINOX_API vinox_status vinox_plugin_std_math_create(
    vinox_tool_plugin** plugin_out
);

VINOX_API vinox_status vinox_plugin_std_time_create(
    vinox_tool_plugin** plugin_out
);

VINOX_API vinox_status vinox_plugin_std_retrieval_create(
    vinox_storage_engine* storage,
    vinox_embedding_engine* embedding,
    vinox_tool_plugin** plugin_out
);

// Dynamic Plugin Loading (Strict Trust & Allowlist: Explicit Path + Optional Expected SHA256)
VINOX_API vinox_status vinox_plugin_load_dynamic(
    const char* library_path,
    const char* expected_sha256,
    vinox_tool_plugin** plugin_out
);

// Diagnostic Last Error API for Plugins
VINOX_API const char* vinox_plugins_last_error(void);

#ifdef __cplusplus
}
#endif

#endif // VINOX_PLUGINS_H
