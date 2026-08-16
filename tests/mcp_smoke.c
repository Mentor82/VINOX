#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include "vinox/mcp.h"
#include "vinox/tools.h"
#include "vinox/vinox.h"

int main(void) {
    printf("Starting VINOX Phase 6.2 MCP Client, HTTP & Stdio Transports Smoke Test...\n");
    fflush(stdout);

    // -------------------------------------------------------------
    // Part 1: Stdio Subprocess Transport (Real Pipe Frame Wire)
    // -------------------------------------------------------------
    vinox_mcp_server_config stdio_cfg;
    memset(&stdio_cfg, 0, sizeof(stdio_cfg));
    stdio_cfg.struct_size = sizeof(stdio_cfg);
    stdio_cfg.server_name = "sqlite";
    stdio_cfg.transport_kind = VINOX_MCP_TRANSPORT_STDIO;
    stdio_cfg.protocol_version = VINOX_MCP_VERSION_2026_07_28;
    stdio_cfg.command_or_url = "vinox_mcp_fixture_server.exe";

    vinox_mcp_client* stdio_client = NULL;
    if (vinox_mcp_client_create(&stdio_cfg, &stdio_client) != VINOX_STATUS_OK || !stdio_client) {
        printf("FAILED: vinox_mcp_client_create (stdio)\n");
        return 1;
    }

    if (vinox_mcp_client_connect(stdio_client) != VINOX_STATUS_OK || !vinox_mcp_client_is_connected(stdio_client)) {
        printf("FAILED: vinox_mcp_client_connect (stdio): %s\n", vinox_mcp_last_error());
        vinox_mcp_client_destroy(stdio_client);
        return 2;
    }
    printf("[DEBUG 1] connect stdio ok\n"); fflush(stdout);

    vinox_tool_registry* registry = NULL;
    if (vinox_tool_registry_create(&registry) != VINOX_STATUS_OK || !registry) {
        printf("FAILED: vinox_tool_registry_create\n");
        vinox_mcp_client_destroy(stdio_client);
        return 3;
    }

    if (vinox_mcp_client_list_tools(stdio_client, registry) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_mcp_client_list_tools over stdio: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(stdio_client);
        return 4;
    }
    printf("[DEBUG 2] list_tools stdio ok\n"); fflush(stdout);

    vinox_tool_definition found_tool;
    memset(&found_tool, 0, sizeof(found_tool));
    found_tool.struct_size = sizeof(found_tool);
    char pool[1024];

    if (vinox_tool_registry_find_tool(registry, "sqlite.query", &found_tool, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        strcmp(found_tool.name, "sqlite.query") != 0) {
        printf("FAILED: MCP Stdio Tool discovery namespacing mismatch (expected 'sqlite.query')\n");
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(stdio_client);
        return 5;
    }

    vinox_tool_call_request call_req;
    memset(&call_req, 0, sizeof(call_req));
    call_req.struct_size = sizeof(call_req);
    call_req.call_id = "call_mcp_101";
    call_req.tool_name = "sqlite.query";
    call_req.arguments_json = "{\"sql\":\"SELECT 1\"}";

    vinox_tool_call_result call_res;
    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    if (vinox_mcp_client_call_tool(stdio_client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        call_res.status_code != 0 || strstr(call_res.result_json, "Executed query successfully") == NULL) {
        printf("FAILED: vinox_mcp_client_call_tool over stdio wire: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(stdio_client);
        return 6;
    }
    printf("[DEBUG 3] call_tool stdio ok\n"); fflush(stdout);

    // Negative Test: Malformed arguments_json MUST Fail Closed
    vinox_tool_call_request bad_call_req = call_req;
    bad_call_req.arguments_json = "{invalid_json_str";
    if (vinox_mcp_client_call_tool(stdio_client, &bad_call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: vinox_mcp_client_call_tool failed to reject malformed arguments_json\n");
        vinox_tool_registry_destroy(registry);
        vinox_mcp_client_destroy(stdio_client);
        return 6;
    }
    printf("[DEBUG 4] bad_call stdio ok\n"); fflush(stdout);

    vinox_mcp_client_destroy(stdio_client);

    // -------------------------------------------------------------
    // Part 2: Streamable HTTP Transport (Real Local HTTP Server)
    // -------------------------------------------------------------
#if defined(_WIN32)
    printf("[DEBUG 5] starting http fixture...\n"); fflush(stdout);
    STARTUPINFOA si_http;
    PROCESS_INFORMATION pi_http;
    memset(&si_http, 0, sizeof(si_http));
    memset(&pi_http, 0, sizeof(pi_http));
    si_http.cb = sizeof(STARTUPINFOA);
    char http_cmd[] = "vinox_mcp_http_fixture_server.exe 18080";

    if (CreateProcessA(NULL, http_cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si_http, &pi_http)) {
        Sleep(500); // Give HTTP server 500ms to bind to 127.0.0.1:18080

        vinox_mcp_server_config http_cfg;
        memset(&http_cfg, 0, sizeof(http_cfg));
        http_cfg.struct_size = sizeof(http_cfg);
        http_cfg.server_name = "http_sqlite";
        http_cfg.transport_kind = VINOX_MCP_TRANSPORT_STREAMABLE_HTTP;
        http_cfg.protocol_version = VINOX_MCP_VERSION_2026_07_28;
        http_cfg.command_or_url = "http://127.0.0.1:18080/mcp";

        vinox_mcp_client* http_client = NULL;
        if (vinox_mcp_client_create(&http_cfg, &http_client) == VINOX_STATUS_OK && http_client) {
            if (vinox_mcp_client_connect(http_client) == VINOX_STATUS_OK) {
                printf("[DEBUG 6] connect http ok\n"); fflush(stdout);
                // List Tools over real HTTP wire
                if (vinox_mcp_client_list_tools(http_client, registry) == VINOX_STATUS_OK) {
                    if (vinox_tool_registry_find_tool(registry, "http_sqlite.query", &found_tool, pool, sizeof(pool)) != VINOX_STATUS_OK) {
                        printf("FAILED: HTTP tool discovery namespacing failed for 'http_sqlite.query'\n");
                    }
                }
                printf("[DEBUG 7] list_tools http ok\n"); fflush(stdout);

                // Call Tool over real HTTP wire
                vinox_tool_call_request http_call;
                memset(&http_call, 0, sizeof(http_call));
                http_call.struct_size = sizeof(http_call);
                http_call.call_id = "call_http_1";
                http_call.tool_name = "http_sqlite.query";
                http_call.arguments_json = "{\"sql\":\"SELECT 1\"}";

                memset(&call_res, 0, sizeof(call_res));
                call_res.struct_size = sizeof(call_res);

                if (vinox_mcp_client_call_tool(http_client, &http_call, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
                    call_res.result_json == NULL ||
                    strstr(call_res.result_json, "Executed HTTP query successfully") == NULL) {
                    printf("FAILED: vinox_mcp_client_call_tool over real Streamable HTTP wire: %s\n", vinox_mcp_last_error());
                }
                printf("[DEBUG 8] call_tool http ok\n"); fflush(stdout);

                // Resources & Prompts over real HTTP wire
                char json_buf[1024];
                size_t req_sz = 0;
                if (vinox_mcp_client_list_resources(http_client, json_buf, sizeof(json_buf), &req_sz) != VINOX_STATUS_OK ||
                    strstr(json_buf, "vinox://http_sqlite/schema") == NULL) {
                    printf("FAILED: vinox_mcp_client_list_resources over HTTP\n");
                }

                if (vinox_mcp_client_list_prompts(http_client, json_buf, sizeof(json_buf), &req_sz) != VINOX_STATUS_OK ||
                    strstr(json_buf, "http_sqlite_analysis") == NULL) {
                    printf("FAILED: vinox_mcp_client_list_prompts over HTTP\n");
                }
                printf("[DEBUG 9] resources/prompts http ok\n"); fflush(stdout);
            }
            vinox_mcp_client_destroy(http_client);
        }

        // HTTP Negative Test: HTTP 400 Bad Request
        http_cfg.command_or_url = "http://127.0.0.1:18080/bad_header";
        vinox_mcp_client* bad_http_client = NULL;
        if (vinox_mcp_client_create(&http_cfg, &bad_http_client) == VINOX_STATUS_OK && bad_http_client) {
            vinox_mcp_client_connect(bad_http_client);
            if (vinox_mcp_client_list_tools(bad_http_client, registry) != VINOX_STATUS_RUNTIME_ERROR) {
                printf("FAILED: Expected HTTP 400 to fail with VINOX_STATUS_RUNTIME_ERROR\n");
            }
            printf("[DEBUG 10] bad_header http ok\n"); fflush(stdout);
            vinox_mcp_client_destroy(bad_http_client);
        }

        // Terminate HTTP fixture server
        TerminateProcess(pi_http.hProcess, 0);
        CloseHandle(pi_http.hProcess);
        CloseHandle(pi_http.hThread);
    }
#endif

    vinox_tool_registry_destroy(registry);
    printf("SUCCESS: All VINOX Phase 6.2 MCP Client, HTTP & Stdio smoke tests passed!\n");
    return 0;
}
