#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vinox/mcp.h"
#include "vinox/tools.h"
#include "vinox/vinox.h"

int main(void) {
    printf("Starting VINOX Phase 6.2 MCP Client & Transports Smoke Test...\n");

    // 1. Config Test Primary Modern MCP 2026-07-28
    vinox_mcp_server_config primary_cfg;
    memset(&primary_cfg, 0, sizeof(primary_cfg));
    primary_cfg.struct_size = sizeof(primary_cfg);
    primary_cfg.server_name = "sqlite";
    primary_cfg.transport_kind = VINOX_MCP_TRANSPORT_STREAMABLE_HTTP;
    primary_cfg.protocol_version = VINOX_MCP_VERSION_2026_07_28;
    primary_cfg.command_or_url = "http://127.0.0.1:8080/mcp";

    vinox_mcp_client* primary_client = NULL;
    if (vinox_mcp_client_create(&primary_cfg, &primary_client) != VINOX_STATUS_OK || !primary_client) {
        printf("FAILED: vinox_mcp_client_create (primary modern)\n");
        return 1;
    }

    if (vinox_mcp_client_connect(primary_client) != VINOX_STATUS_OK || !vinox_mcp_client_is_connected(primary_client)) {
        printf("FAILED: vinox_mcp_client_connect (primary modern)\n");
        vinox_mcp_client_destroy(primary_client);
        return 2;
    }

    // 2. Tool Discovery & Namespacing into Tool Registry
    vinox_tool_registry* registry = NULL;
    if (vinox_tool_registry_create(&registry) != VINOX_STATUS_OK || !registry) {
        printf("FAILED: vinox_tool_registry_create\n");
        vinox_mcp_client_destroy(primary_client);
        return 3;
    }

    if (vinox_mcp_client_list_tools(primary_client, registry) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_mcp_client_list_tools\n");
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(primary_client);
        return 4;
    }

    vinox_tool_definition found_tool;
    memset(&found_tool, 0, sizeof(found_tool));
    found_tool.struct_size = sizeof(found_tool);
    char pool[1024];

    // Assert tool was namespaced as "sqlite.query"
    if (vinox_tool_registry_find_tool(registry, "sqlite.query", &found_tool, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        strcmp(found_tool.name, "sqlite.query") != 0) {
        printf("FAILED: MCP Tool discovery namespacing mismatch (expected 'sqlite.query')\n");
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(primary_client);
        return 5;
    }

    // 3. MCP Tool Call Execution
    vinox_tool_call_request call_req;
    memset(&call_req, 0, sizeof(call_req));
    call_req.struct_size = sizeof(call_req);
    call_req.call_id = "call_mcp_101";
    call_req.tool_name = "sqlite.query";
    call_req.arguments_json = "{\"sql\":\"SELECT 1\"}";

    vinox_tool_call_result call_res;
    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    if (vinox_mcp_client_call_tool(primary_client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        call_res.status_code != 0 || strstr(call_res.result_json, "sqlite.query") == NULL) {
        printf("FAILED: vinox_mcp_client_call_tool\n");
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(primary_client);
        return 6;
    }

    // 4. Resources Primitive API
    char json_buf[1024];
    size_t req_sz = 0;
    if (vinox_mcp_client_list_resources(primary_client, json_buf, sizeof(json_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(json_buf, "vinox://sqlite/schema") == NULL) {
        printf("FAILED: vinox_mcp_client_list_resources (got '%s')\n", json_buf);
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(primary_client);
        return 7;
    }

    if (vinox_mcp_client_read_resource(primary_client, "vinox://sqlite/schema", json_buf, sizeof(json_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(json_buf, "vinox://sqlite/schema") == NULL) {
        printf("FAILED: vinox_mcp_client_read_resource\n");
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(primary_client);
        return 8;
    }

    // 5. Prompts Primitive API
    if (vinox_mcp_client_list_prompts(primary_client, json_buf, sizeof(json_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(json_buf, "sqlite_analysis") == NULL) {
        printf("FAILED: vinox_mcp_client_list_prompts\n");
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(primary_client);
        return 9;
    }

    if (vinox_mcp_client_get_prompt(primary_client, "sqlite_analysis", "{}", json_buf, sizeof(json_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(json_buf, "sqlite_analysis") == NULL) {
        printf("FAILED: vinox_mcp_client_get_prompt\n");
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(primary_client);
        return 10;
    }

    vinox_mcp_client_destroy(primary_client);

    // 6. Config Test Legacy Compatibility Mode (MCP 2024-11-05 + SSE)
    vinox_mcp_server_config legacy_cfg;
    memset(&legacy_cfg, 0, sizeof(legacy_cfg));
    legacy_cfg.struct_size = sizeof(legacy_cfg);
    legacy_cfg.server_name = "legacy_fs";
    legacy_cfg.transport_kind = VINOX_MCP_TRANSPORT_LEGACY_SSE;
    legacy_cfg.protocol_version = VINOX_MCP_VERSION_2024_11_05;
    legacy_cfg.command_or_url = "http://127.0.0.1:8080/sse";
    legacy_cfg.legacy_handshake_enabled = 1;
    legacy_cfg.legacy_sse_enabled = 1;

    vinox_mcp_client* legacy_client = NULL;
    if (vinox_mcp_client_create(&legacy_cfg, &legacy_client) != VINOX_STATUS_OK || !legacy_client) {
        printf("FAILED: vinox_mcp_client_create (legacy mode)\n");
        vinox_tool_registry_destroy(registry);
        return 11;
    }

    if (vinox_mcp_client_connect(legacy_client) != VINOX_STATUS_OK || !vinox_mcp_client_is_connected(legacy_client)) {
        printf("FAILED: vinox_mcp_client_connect (legacy mode)\n");
        vinox_mcp_client_destroy(legacy_client);
        vinox_tool_registry_destroy(registry);
        return 12;
    }

    vinox_mcp_client_destroy(legacy_client);
    vinox_tool_registry_destroy(registry);

    printf("SUCCESS: All VINOX Phase 6.2 MCP Client & Transports smoke tests passed!\n");
    return 0;
}
