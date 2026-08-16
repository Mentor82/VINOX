#include "vinox/mcp.h"
#include "vinox/tools.h"
#include "vinox/vinox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("Starting VINOX Phase 6.3 Standalone MCP Server Smoke Test...\n");

    vinox_mcp_server_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = sizeof(cfg);
    cfg.server_name = "vinox_mcp";
    cfg.transport_kind = VINOX_MCP_TRANSPORT_STDIO;
    cfg.command_or_url = "vinox_mcp_server.exe";
    cfg.protocol_version = VINOX_MCP_VERSION_2026_07_28;

    vinox_mcp_client* client = NULL;
    if (vinox_mcp_client_create(&cfg, &client) != VINOX_STATUS_OK || !client) {
        printf("FAILED: vinox_mcp_client_create returned NULL\n");
        return 1;
    }

    if (vinox_mcp_client_connect(client) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_mcp_client_connect to vinox_mcp_server.exe failed: %s\n", vinox_mcp_last_error());
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Standalone MCP Server Stdio Pipe Connection: Verified\n");

    /* 1. Discover Tools */
    vinox_tool_registry* reg = NULL;
    if (vinox_tool_registry_create(&reg) != VINOX_STATUS_OK || !reg) {
        printf("FAILED: vinox_tool_registry_create failed\n");
        vinox_mcp_client_destroy(client);
        return 1;
    }

    if (vinox_mcp_client_list_tools(client, reg) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_mcp_client_list_tools failed: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }

    vinox_tool_definition tool_def;
    memset(&tool_def, 0, sizeof(tool_def));
    tool_def.struct_size = sizeof(tool_def);

    char tool_pool[2048];
    if (vinox_tool_registry_find_tool(reg, "vinox_mcp.vinox.search", &tool_def, tool_pool, sizeof(tool_pool)) != VINOX_STATUS_OK) {
        printf("FAILED: Could not find vinox_mcp.vinox.search in tool registry\n");
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - VINOX Native MCP Tool Discovery (vinox.search, vinox.conversation_get, etc.): Verified\n");

    /* 2. Execute vinox.search Tool */
    vinox_tool_call_request call_req;
    memset(&call_req, 0, sizeof(call_req));
    call_req.struct_size = sizeof(call_req);
    call_req.call_id = "call_search_1";
    call_req.tool_name = "vinox_mcp.vinox.search";
    call_req.arguments_json = "{\"query\":\"hybrid search\"}";

    vinox_tool_call_result call_res;
    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    char pool[4096];
    if (vinox_mcp_client_call_tool(client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        call_res.result_json == NULL ||
        strstr(call_res.result_json, "VINOX Hybrid Search Result") == NULL) {
        printf("FAILED: vinox.search tool execution failed: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Real Wire vinox.search Hybrid Retrieval Execution: Verified\n");

    /* 3. Verify Default-Deny Write Tool Policy */
    call_req.call_id = "call_ingest_1";
    call_req.tool_name = "vinox_mcp.vinox.document_ingest";
    call_req.arguments_json = "{\"title\":\"doc.txt\",\"content\":\"secret\"}";

    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    if (vinox_mcp_client_call_tool(client, &call_req, &call_res, pool, sizeof(pool)) == VINOX_STATUS_OK &&
        call_res.status_code == 0) {
        printf("FAILED: vinox.document_ingest must be rejected by default-deny policy\n");
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Default-Deny Security Policy Rejection of Write Tools: Verified\n");

    /* 4. MCP Resources List & Read */
    char res_buf[4096] = {0};
    size_t req_sz = 0;
    if (vinox_mcp_client_list_resources(client, res_buf, sizeof(res_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(res_buf, "vinox://conversations/sample") == NULL) {
        printf("FAILED: vinox_mcp_client_list_resources failed\n");
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }

    char content_buf[4096] = {0};
    if (vinox_mcp_client_read_resource(client, "vinox://conversations/sample", content_buf, sizeof(content_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(content_buf, "Canonical VINOX Resource Content") == NULL) {
        printf("FAILED: vinox_mcp_client_read_resource failed\n");
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Native VINOX MCP Resources (vinox://conversations/sample) List/Read: Verified\n");

    /* Cleanup */
    vinox_tool_registry_destroy(reg);
    vinox_mcp_client_destroy(client);

    printf("SUCCESS: All VINOX Phase 6.3 Standalone MCP Server smoke tests passed!\n");
    return 0;
}
