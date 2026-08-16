#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vinox/mcp.h"
#include "vinox/tools.h"
#include "vinox/vinox.h"

int main(void) {
    printf("Starting VINOX Phase 6.2 MCP Client & Transports Smoke Test...\n");

    // 1. Stdio Transport with Real Fixture Server Process
    vinox_mcp_server_config stdio_cfg;
    memset(&stdio_cfg, 0, sizeof(stdio_cfg));
    stdio_cfg.struct_size = sizeof(stdio_cfg);
    stdio_cfg.server_name = "sqlite";
    stdio_cfg.transport_kind = VINOX_MCP_TRANSPORT_STDIO;
    stdio_cfg.protocol_version = VINOX_MCP_VERSION_2026_07_28;
    stdio_cfg.command_or_url = "vinox_mcp_fixture_server.exe";

    vinox_mcp_client* client = NULL;
    if (vinox_mcp_client_create(&stdio_cfg, &client) != VINOX_STATUS_OK || !client) {
        printf("FAILED: vinox_mcp_client_create (stdio modern)\n");
        return 1;
    }

    if (vinox_mcp_client_connect(client) != VINOX_STATUS_OK || !vinox_mcp_client_is_connected(client)) {
        printf("FAILED: vinox_mcp_client_connect (stdio modern): %s\n", vinox_mcp_last_error());
        vinox_mcp_client_destroy(client);
        return 2;
    }

    // 2. Tool Discovery over Real Wire Stdio Pipes
    vinox_tool_registry* registry = NULL;
    if (vinox_tool_registry_create(&registry) != VINOX_STATUS_OK || !registry) {
        printf("FAILED: vinox_tool_registry_create\n");
        vinox_mcp_client_destroy(client);
        return 3;
    }

    if (vinox_mcp_client_list_tools(client, registry) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_mcp_client_list_tools over stdio: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(client);
        return 4;
    }

    vinox_tool_definition found_tool;
    memset(&found_tool, 0, sizeof(found_tool));
    found_tool.struct_size = sizeof(found_tool);
    char pool[1024];

    if (vinox_tool_registry_find_tool(registry, "sqlite.query", &found_tool, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        strcmp(found_tool.name, "sqlite.query") != 0) {
        printf("FAILED: MCP Tool discovery namespacing mismatch (expected 'sqlite.query')\n");
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(client);
        return 5;
    }

    // 3. Real MCP Tool Call Execution over Stdio Wire Pipes
    vinox_tool_call_request call_req;
    memset(&call_req, 0, sizeof(call_req));
    call_req.struct_size = sizeof(call_req);
    call_req.call_id = "call_mcp_101";
    call_req.tool_name = "sqlite.query";
    call_req.arguments_json = "{\"sql\":\"SELECT 1\"}";

    vinox_tool_call_result call_res;
    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    if (vinox_mcp_client_call_tool(client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        call_res.status_code != 0 || strstr(call_res.result_json, "Executed query successfully") == NULL) {
        printf("FAILED: vinox_mcp_client_call_tool over stdio wire: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(client);
        return 6;
    }

    // 4. Real Resources Primitive API over Stdio Wire Pipes
    char json_buf[1024];
    size_t req_sz = 0;
    if (vinox_mcp_client_list_resources(client, json_buf, sizeof(json_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(json_buf, "vinox://sqlite/schema") == NULL) {
        printf("FAILED: vinox_mcp_client_list_resources over stdio wire (got '%s'): %s\n", json_buf, vinox_mcp_last_error());
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(client);
        return 7;
    }

    if (vinox_mcp_client_read_resource(client, "vinox://sqlite/schema", json_buf, sizeof(json_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(json_buf, "CREATE TABLE test;") == NULL) {
        printf("FAILED: vinox_mcp_client_read_resource over stdio wire: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(client);
        return 8;
    }

    // 5. Real Prompts Primitive API over Stdio Wire Pipes
    if (vinox_mcp_client_list_prompts(client, json_buf, sizeof(json_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(json_buf, "sqlite_analysis") == NULL) {
        printf("FAILED: vinox_mcp_client_list_prompts over stdio wire: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(client);
        return 9;
    }

    if (vinox_mcp_client_get_prompt(client, "sqlite_analysis", "{}", json_buf, sizeof(json_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(json_buf, "Rendered analysis prompt") == NULL) {
        printf("FAILED: vinox_mcp_client_get_prompt over stdio wire: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(client);
        return 10;
    }

    vinox_mcp_client_destroy(client);

    // 6. Test Legacy Compatibility Mode (2024-11-05 Handshake + notifications/initialized)
    vinox_mcp_server_config legacy_cfg;
    memset(&legacy_cfg, 0, sizeof(legacy_cfg));
    legacy_cfg.struct_size = sizeof(legacy_cfg);
    legacy_cfg.server_name = "legacy_fs";
    legacy_cfg.transport_kind = VINOX_MCP_TRANSPORT_STDIO;
    legacy_cfg.protocol_version = VINOX_MCP_VERSION_2024_11_05;
    legacy_cfg.command_or_url = "vinox_mcp_fixture_server.exe";
    legacy_cfg.legacy_handshake_enabled = 1;

    vinox_mcp_client* legacy_client = NULL;
    if (vinox_mcp_client_create(&legacy_cfg, &legacy_client) != VINOX_STATUS_OK || !legacy_client) {
        printf("FAILED: vinox_mcp_client_create (legacy handshake mode)\n");
        vinox_tool_registry_destroy(registry);
        return 11;
    }

    if (vinox_mcp_client_connect(legacy_client) != VINOX_STATUS_OK || !vinox_mcp_client_is_connected(legacy_client)) {
        printf("FAILED: vinox_mcp_client_connect (legacy handshake mode): %s\n", vinox_mcp_last_error());
        vinox_mcp_client_destroy(legacy_client);
        vinox_tool_registry_destroy(registry);
        return 12;
    }

    vinox_mcp_client_destroy(legacy_client);
    vinox_tool_registry_destroy(registry);

    printf("SUCCESS: All VINOX Phase 6.2 MCP Client & Transports smoke tests passed!\n");
    return 0;
}
