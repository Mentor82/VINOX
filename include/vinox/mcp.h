#ifndef VINOX_MCP_H
#define VINOX_MCP_H

#include <stddef.h>
#include <stdint.h>

#include "vinox/export.h"
#include "vinox/logging.h"
#include "vinox/tools.h"
#include "vinox/vinox.h"

#ifdef __cplusplus
extern "C" {
#endif

// MCP Transport kinds
typedef enum vinox_mcp_transport_kind {
    VINOX_MCP_TRANSPORT_STDIO = 0,
    VINOX_MCP_TRANSPORT_STREAMABLE_HTTP = 1,
    VINOX_MCP_TRANSPORT_LEGACY_SSE = 2
} vinox_mcp_transport_kind;

// MCP Protocol Revision Versions
typedef enum vinox_mcp_protocol_version {
    VINOX_MCP_VERSION_2026_07_28 = 0, // Primary modern stateless revision
    VINOX_MCP_VERSION_2024_11_05 = 1  // Legacy stateful handshake revision
} vinox_mcp_protocol_version;

// MCP Server Configuration Struct (Prefix-Layout ABI)
typedef struct vinox_mcp_server_config {
    uint32_t struct_size;               // Size of struct for ABI compatibility
    const char* server_name;            // Server namespace identifier e.g. "sqlite"
    uint32_t transport_kind;            // vinox_mcp_transport_kind
    uint32_t protocol_version;          // vinox_mcp_protocol_version
    const char* command_or_url;         // Command e.g. "npx -y @modelcontextprotocol/server-sqlite" or HTTP URL
    const char* working_dir;            // Working directory for stdio process (optional)
    const char* env_vars_json;          // Whitelisted environment variables JSON object
    int32_t legacy_handshake_enabled;   // 1 to execute initialize/initialized handshake (legacy compatibility)
    int32_t legacy_sse_enabled;         // 1 to manage Mcp-Session-Id header & GET-SSE streaming
} vinox_mcp_server_config;

#define VINOX_MCP_SERVER_CONFIG_MIN_SIZE \
    ((uint32_t)(offsetof(vinox_mcp_server_config, legacy_sse_enabled) + sizeof(int32_t)))

// Opaque MCP Client handle
typedef struct vinox_mcp_client vinox_mcp_client;

// MCP Client C-ABI Lifecycle API
VINOX_API vinox_status vinox_mcp_client_create(const vinox_mcp_server_config* config, vinox_mcp_client** client_out);
VINOX_API vinox_status vinox_mcp_client_destroy(vinox_mcp_client* client);
VINOX_API vinox_status vinox_mcp_client_connect(vinox_mcp_client* client);
VINOX_API vinox_status vinox_mcp_client_disconnect(vinox_mcp_client* client);
VINOX_API int32_t vinox_mcp_client_is_connected(const vinox_mcp_client* client);

// MCP Tool Discovery & Execution API
VINOX_API vinox_status vinox_mcp_client_list_tools(vinox_mcp_client* client, vinox_tool_registry* target_registry);
VINOX_API vinox_status vinox_mcp_client_call_tool(
    vinox_mcp_client* client,
    const vinox_tool_call_request* request,
    vinox_tool_call_result* result_out,
    char* pool_buf,
    size_t pool_buf_size
);

// MCP Resources Primitive API
VINOX_API vinox_status vinox_mcp_client_list_resources(vinox_mcp_client* client, char* json_out, size_t json_out_size, size_t* required_size_out);
VINOX_API vinox_status vinox_mcp_client_read_resource(
    vinox_mcp_client* client,
    const char* uri,
    char* content_out,
    size_t content_out_size,
    size_t* required_size_out
);

// MCP Prompts Primitive API
VINOX_API vinox_status vinox_mcp_client_list_prompts(vinox_mcp_client* client, char* json_out, size_t json_out_size, size_t* required_size_out);
VINOX_API vinox_status vinox_mcp_client_get_prompt(
    vinox_mcp_client* client,
    const char* prompt_name,
    const char* args_json,
    char* content_out,
    size_t content_out_size,
    size_t* required_size_out
);

// Diagnostic Last Error API for MCP Module
VINOX_API const char* vinox_mcp_last_error(void);

#ifdef __cplusplus
}
#endif

#endif // VINOX_MCP_H
