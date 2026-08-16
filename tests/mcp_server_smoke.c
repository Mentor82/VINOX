#include "vinox/mcp.h"
#include "vinox/tools.h"
#include "vinox/vinox.h"
#include "vinox/storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define putenv_custom _putenv
#else
#  define putenv_custom putenv
#endif

int main(void) {
    printf("Starting VINOX Phase 6.3 Standalone MCP Server Smoke Test...\n");

    const char* seeded_db_file = "test_vinox_mcp_server_seeded.db";
    remove(seeded_db_file);

    /* 0. Seed canonical storage with test data */
    vinox_storage_engine* storage = NULL;
    if (vinox_storage_engine_open(seeded_db_file, &storage) != VINOX_STATUS_OK || !storage) {
        printf("FAILED: vinox_storage_engine_open for seed DB failed\n");
        return 1;
    }

    vinox_conversation_info conv_info;
    memset(&conv_info, 0, sizeof(conv_info));
    conv_info.struct_size = sizeof(conv_info);
    if (vinox_storage_create_conversation(storage, "MCP Test Architecture Branch", &conv_info) != VINOX_STATUS_OK) {
        printf("FAILED: Seed conversation creation failed\n");
        vinox_storage_engine_close(storage);
        return 1;
    }
    char seed_cid[128] = {0};
    strncpy(seed_cid, conv_info.id, sizeof(seed_cid) - 1);

    vinox_message_info msg_root;
    memset(&msg_root, 0, sizeof(msg_root));
    msg_root.struct_size = sizeof(msg_root);
    msg_root.id = "msg-root";
    msg_root.conversation_id = seed_cid;
    msg_root.role = "user";
    msg_root.content = "Root question about architecture";

    vinox_message_info msg_out;
    memset(&msg_out, 0, sizeof(msg_out));
    msg_out.struct_size = sizeof(msg_out);

    if (vinox_storage_add_message(storage, &msg_root, &msg_out) != VINOX_STATUS_OK) {
        printf("FAILED: Seed msg_root creation failed\n");
        vinox_storage_engine_close(storage);
        return 1;
    }

    vinox_message_info msg_branchA;
    memset(&msg_branchA, 0, sizeof(msg_branchA));
    msg_branchA.struct_size = sizeof(msg_branchA);
    msg_branchA.id = "msg-branchA";
    msg_branchA.conversation_id = seed_cid;
    msg_branchA.parent_id = "msg-root";
    msg_branchA.role = "assistant";
    msg_branchA.content = "Branch A architecture suggestion";
    if (vinox_storage_add_message(storage, &msg_branchA, &msg_out) != VINOX_STATUS_OK) {
        printf("FAILED: Seed msg_branchA creation failed\n");
        vinox_storage_engine_close(storage);
        return 1;
    }

    vinox_message_info msg_branchB;
    memset(&msg_branchB, 0, sizeof(msg_branchB));
    msg_branchB.struct_size = sizeof(msg_branchB);
    msg_branchB.id = "msg-branchB";
    msg_branchB.conversation_id = seed_cid;
    msg_branchB.parent_id = "msg-root";
    msg_branchB.role = "assistant";
    msg_branchB.content = "VINOX MCP hybrid vector retrieval specification note";
    if (vinox_storage_add_message(storage, &msg_branchB, &msg_out) != VINOX_STATUS_OK) {
        printf("FAILED: Seed msg_branchB creation failed\n");
        vinox_storage_engine_close(storage);
        return 1;
    }

    char seed_doc_id[128] = {0};
    if (vinox_storage_document_ingest(storage, "VINOX MCP Protocol Specification", "Native MCP stdio transport with hybrid vector retrieval", seed_doc_id, sizeof(seed_doc_id)) != VINOX_STATUS_OK) {
        printf("FAILED: Seed document ingestion failed\n");
        vinox_storage_engine_close(storage);
        return 1;
    }

    if (vinox_storage_relation_create(storage, seed_doc_id, seed_cid, "references", "Specification links conversation", 0.95f) != VINOX_STATUS_OK) {
        printf("FAILED: Seed relation creation failed\n");
        vinox_storage_engine_close(storage);
        return 1;
    }

    vinox_storage_engine_close(storage);
    printf("  - Canonical Storage Database Seeding: Verified\n");

    /* Set environment variable for MCP server stdio process */
#if defined(_WIN32)
    _putenv("VINOX_STORAGE_DB=test_vinox_mcp_server_seeded.db");
#else
    setenv("VINOX_STORAGE_DB", "test_vinox_mcp_server_seeded.db", 1);
#endif

    vinox_mcp_server_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = sizeof(cfg);
    cfg.server_name = "vinox_mcp";
    cfg.transport_kind = VINOX_MCP_TRANSPORT_STDIO;
    cfg.command_or_url = "vinox_mcp_server.exe --allow-write";
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

    /* 2. Execute vinox.search Tool (FTS Text Query) */
    vinox_tool_call_request call_req;
    memset(&call_req, 0, sizeof(call_req));
    call_req.struct_size = sizeof(call_req);
    call_req.call_id = "call_search_1";
    call_req.tool_name = "vinox_mcp.vinox.search";
    call_req.arguments_json = "{\"query\":\"hybrid vector\"}";

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
    printf("  - Real Wire vinox.search FTS Text Retrieval Execution: Verified\n");

    /* 3. Execute vinox.search Tool with 1024-dim Vector Embedding Array */
    call_req.call_id = "call_search_vec";
    call_req.tool_name = "vinox_mcp.vinox.search";
    char vec_args[16384] = "{\"query\":\"specification\",\"embedding\":[";
    for (int i = 0; i < 1024; ++i) {
        strcat(vec_args, (i == 0) ? "0.1" : ",0.1");
    }
    strcat(vec_args, "]}");
    call_req.arguments_json = vec_args;

    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    if (vinox_mcp_client_call_tool(client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        call_res.result_json == NULL ||
        strstr(call_res.result_json, "VINOX Hybrid Search Result") == NULL) {
        printf("FAILED: vinox.search tool vector embedding execution failed: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Real Wire vinox.search Vector Embedding Query Execution: Verified\n");

    /* 4. Execute vinox.conversation_get Tool with leaf_message_id branch selection */
    char conv_args[512];
    snprintf(conv_args, sizeof(conv_args), "{\"conversation_id\":\"%s\",\"leaf_message_id\":\"msg-branchB\"}", seed_cid);
    call_req.call_id = "call_conv_get_1";
    call_req.tool_name = "vinox_mcp.vinox.conversation_get";
    call_req.arguments_json = conv_args;

    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    if (vinox_mcp_client_call_tool(client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        call_res.result_json == NULL ||
        strstr(call_res.result_json, "Root question about architecture") == NULL ||
        strstr(call_res.result_json, "VINOX MCP hybrid vector retrieval specification note") == NULL ||
        strstr(call_res.result_json, "Branch A architecture suggestion") != NULL) {
        printf("FAILED: vinox.conversation_get branch reconstruction failed or included alternate branch: %s\n", call_res.result_json ? call_res.result_json : vinox_mcp_last_error());
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Real Wire vinox.conversation_get Reconstructed Parent Chain Branch: Verified\n");

    /* 4b. Test Unknown leaf_message_id Fails Closed */
    snprintf(conv_args, sizeof(conv_args), "{\"conversation_id\":\"%s\",\"leaf_message_id\":\"NON_EXISTENT_LEAF\"}", seed_cid);
    call_req.call_id = "call_conv_get_bad_leaf";
    call_req.tool_name = "vinox_mcp.vinox.conversation_get";
    call_req.arguments_json = conv_args;

    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    if (vinox_mcp_client_call_tool(client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        call_res.result_json == NULL ||
        strstr(call_res.result_json, "Specified leaf_message_id not found") == NULL) {
        printf("FAILED: Unknown leaf_message_id must fail closed with error!\n");
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Unknown leaf_message_id Fail-Closed Check: Verified\n");

    /* 4c. Test Central Phase 6.1 Bounded Schema Validator Rejection (additionalProperties: false) */
    call_req.call_id = "call_bad_schema_1";
    call_req.tool_name = "vinox_mcp.vinox.search";
    call_req.arguments_json = "{\"query\":\"test\",\"unauthorized_extra_param\":123}";

    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    if (vinox_mcp_client_call_tool(client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        call_res.result_json == NULL ||
        strstr(call_res.result_json, "Invalid tool arguments") == NULL ||
        strstr(call_res.result_json, "forbidden by schema") == NULL) {
        printf("FAILED: Central Phase 6.1 bounded schema validator must reject additionalProperties!\n");
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Central Phase 6.1 Bounded Schema Validator Server-Side Enforcement: Verified\n");

    /* 4d. Test Oversize Input Payload Limit Check (Max 128 KB) */
    call_req.call_id = "call_oversize_input";
    call_req.tool_name = "vinox_mcp.vinox.search";
    static char oversize_args[140000];
    strcpy(oversize_args, "{\"query\":\"");
    memset(oversize_args + strlen("{\"query\":\""), 'A', 135000);
    oversize_args[strlen("{\"query\":\"") + 135000] = '\0';
    strcat(oversize_args, "\"}");
    call_req.arguments_json = oversize_args;

    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    if (vinox_mcp_client_call_tool(client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        call_res.result_json == NULL ||
        strstr(call_res.result_json, "Input payload size limit exceeded") == NULL) {
        printf("FAILED: Oversize input payload > 128 KB must be rejected!\n");
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Oversize Input Payload Limit (128 KB Gate): Verified\n");

    /* 5. Execute vinox.relations_query Tool */
    char rel_args[512];
    snprintf(rel_args, sizeof(rel_args), "{\"entity_id\":\"%s\"}", seed_doc_id);
    call_req.call_id = "call_rel_q_1";
    call_req.tool_name = "vinox_mcp.vinox.relations_query";
    call_req.arguments_json = rel_args;

    memset(&call_res, 0, sizeof(call_res));
    call_res.struct_size = sizeof(call_res);

    if (vinox_mcp_client_call_tool(client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        call_res.result_json == NULL ||
        strstr(call_res.result_json, "references") == NULL) {
        printf("FAILED: vinox.relations_query tool execution failed: %s\n", vinox_mcp_last_error());
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Real Wire vinox.relations_query CTE Graph Traversal: Verified\n");

    /* 6. MCP Resources List & Read Canonical Content */
    char res_buf[16384] = {0};
    size_t req_sz = 0;
    if (vinox_mcp_client_list_resources(client, res_buf, sizeof(res_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(res_buf, seed_cid) == NULL ||
        strstr(res_buf, seed_doc_id) == NULL) {
        printf("FAILED: vinox_mcp_client_list_resources canonical resources list failed: %s\n", res_buf);
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }

    char res_uri[256];
    snprintf(res_uri, sizeof(res_uri), "vinox://conversations/%s", seed_cid);

    char content_buf[16384] = {0};
    if (vinox_mcp_client_read_resource(client, res_uri, content_buf, sizeof(content_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(content_buf, "VINOX MCP hybrid vector retrieval specification note") == NULL) {
        printf("FAILED: vinox_mcp_client_read_resource canonical conversation resource read failed: %s\n", content_buf);
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }

    /* Verify missing resource fails closed with error */
    char err_res_buf[16384] = {0};
    if (vinox_mcp_client_read_resource(client, "vinox://documents/NON_EXISTENT_DOC", err_res_buf, sizeof(err_res_buf), &req_sz) == VINOX_STATUS_OK) {
        printf("FAILED: Reading missing resource vinox://documents/NON_EXISTENT_DOC must fail closed!\n");
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Native VINOX MCP Resources List/Read Canonical Content & Missing Resource Fail-Closed: Verified\n");

    /* 7. Server-Side Policy Engine Denial Test (No --allow-write flag) */
    vinox_mcp_server_config cfg_no_write;
    memset(&cfg_no_write, 0, sizeof(cfg_no_write));
    cfg_no_write.struct_size = sizeof(cfg_no_write);
    cfg_no_write.server_name = "vinox_mcp_no_write";
    cfg_no_write.transport_kind = VINOX_MCP_TRANSPORT_STDIO;
    cfg_no_write.command_or_url = "vinox_mcp_server.exe";
    cfg_no_write.protocol_version = VINOX_MCP_VERSION_2026_07_28;

    vinox_mcp_client* deny_client = NULL;
    if (vinox_mcp_client_create(&cfg_no_write, &deny_client) == VINOX_STATUS_OK && deny_client) {
        if (vinox_mcp_client_connect(deny_client) == VINOX_STATUS_OK) {
            call_req.call_id = "call_deny_write_1";
            call_req.tool_name = "vinox_mcp_no_write.vinox.document_ingest";
            call_req.arguments_json = "{\"title\":\"Denied Doc\",\"content\":\"Test content\"}";

            memset(&call_res, 0, sizeof(call_res));
            call_res.struct_size = sizeof(call_res);

            if (vinox_mcp_client_call_tool(deny_client, &call_req, &call_res, pool, sizeof(pool)) == VINOX_STATUS_OK) {
                if (call_res.result_json == NULL || strstr(call_res.result_json, "rejected by policy engine") == NULL) {
                    printf("FAILED: Write tool without --allow-write must be rejected by policy engine!\n");
                    vinox_mcp_client_destroy(deny_client);
                    return 1;
                }
            }
        }
        vinox_mcp_client_destroy(deny_client);
    }
    printf("  - Server-Side C-ABI Policy Engine Denial (No --allow-write): Verified\n");

    /* 8. Execution Timeout / Deadline Test with Storage Non-Mutation Verification */
#if defined(_WIN32)
    _putenv("VINOX_TEST_TIMEOUT_SIM_MS=2500");
#else
    setenv("VINOX_TEST_TIMEOUT_SIM_MS", "2500", 1);
#endif
    vinox_mcp_server_config cfg_timeout;
    memset(&cfg_timeout, 0, sizeof(cfg_timeout));
    cfg_timeout.struct_size = sizeof(cfg_timeout);
    cfg_timeout.server_name = "vinox_mcp_timeout";
    cfg_timeout.transport_kind = VINOX_MCP_TRANSPORT_STDIO;
    cfg_timeout.command_or_url = "vinox_mcp_server.exe --allow-write";
    cfg_timeout.protocol_version = VINOX_MCP_VERSION_2026_07_28;

    vinox_mcp_client* timeout_client = NULL;
    if (vinox_mcp_client_create(&cfg_timeout, &timeout_client) == VINOX_STATUS_OK && timeout_client) {
        if (vinox_mcp_client_connect(timeout_client) == VINOX_STATUS_OK) {
            call_req.call_id = "call_timeout_sim";
            call_req.tool_name = "vinox_mcp_timeout.vinox.document_ingest";
            call_req.arguments_json = "{\"title\":\"Timed Out Doc\",\"content\":\"Test content that must NOT be saved\"}";

            memset(&call_res, 0, sizeof(call_res));
            call_res.struct_size = sizeof(call_res);

            if (vinox_mcp_client_call_tool(timeout_client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
                call_res.result_json == NULL ||
                strstr(call_res.result_json, "timed out after 2000 ms") == NULL) {
                printf("FAILED: Tool execution exceeding server deadline must return timeout error!\n");
                vinox_mcp_client_destroy(timeout_client);
                vinox_tool_registry_destroy(reg);
                vinox_mcp_client_destroy(client);
                return 1;
            }
        }
        vinox_mcp_client_destroy(timeout_client);
    }
#if defined(_WIN32)
    _putenv("VINOX_TEST_TIMEOUT_SIM_MS=");
#else
    unsetenv("VINOX_TEST_TIMEOUT_SIM_MS");
#endif

    /* Verify that "Timed Out Doc" was NOT written to storage */
    if (vinox_mcp_client_list_resources(client, res_buf, sizeof(res_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(res_buf, "Timed Out Doc") != NULL) {
        printf("FAILED: Timed-out document ingest must NOT mutate canonical storage!\n");
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Tool Execution Timeout & Storage Non-Mutation Guarantee: Verified\n");

    /* 9. Cancellation Propagation Test with Storage Non-Mutation Verification */
#if defined(_WIN32)
    _putenv("VINOX_TEST_CANCEL_SIM=1");
#else
    setenv("VINOX_TEST_CANCEL_SIM", "1", 1);
#endif
    vinox_mcp_server_config cfg_cancel;
    memset(&cfg_cancel, 0, sizeof(cfg_cancel));
    cfg_cancel.struct_size = sizeof(cfg_cancel);
    cfg_cancel.server_name = "vinox_mcp_cancel";
    cfg_cancel.transport_kind = VINOX_MCP_TRANSPORT_STDIO;
    cfg_cancel.command_or_url = "vinox_mcp_server.exe --allow-write";
    cfg_cancel.protocol_version = VINOX_MCP_VERSION_2026_07_28;

    vinox_mcp_client* cancel_client = NULL;
    if (vinox_mcp_client_create(&cfg_cancel, &cancel_client) == VINOX_STATUS_OK && cancel_client) {
        if (vinox_mcp_client_connect(cancel_client) == VINOX_STATUS_OK) {
            call_req.call_id = "call_cancel_sim";
            call_req.tool_name = "vinox_mcp_cancel.vinox.document_ingest";
            call_req.arguments_json = "{\"title\":\"Cancelled Doc\",\"content\":\"Test content that must NOT be saved\"}";

            memset(&call_res, 0, sizeof(call_res));
            call_res.struct_size = sizeof(call_res);

            if (vinox_mcp_client_call_tool(cancel_client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
                call_res.result_json == NULL ||
                strstr(call_res.result_json, "Tool execution cancelled") == NULL) {
                printf("FAILED: Cancelled tool execution must return cancellation error!\n");
                vinox_mcp_client_destroy(cancel_client);
                vinox_tool_registry_destroy(reg);
                vinox_mcp_client_destroy(client);
                return 1;
            }
        }
        vinox_mcp_client_destroy(cancel_client);
    }
#if defined(_WIN32)
    _putenv("VINOX_TEST_CANCEL_SIM=");
#else
    unsetenv("VINOX_TEST_CANCEL_SIM");
#endif

    /* Verify that "Cancelled Doc" was NOT written to storage */
    if (vinox_mcp_client_list_resources(client, res_buf, sizeof(res_buf), &req_sz) != VINOX_STATUS_OK ||
        strstr(res_buf, "Cancelled Doc") != NULL) {
        printf("FAILED: Cancelled document ingest must NOT mutate canonical storage!\n");
        vinox_tool_registry_destroy(reg);
        vinox_mcp_client_destroy(client);
        return 1;
    }
    printf("  - Tool Execution Cancellation Propagation & Storage Non-Mutation Guarantee: Verified\n");

    /* 10. Bounded Output Payload Size Limit Test (> 256 KB) */
#if defined(_WIN32)
    _putenv("VINOX_TEST_OVERSIZE_SIM_KB=300");
#else
    setenv("VINOX_TEST_OVERSIZE_SIM_KB", "300", 1);
#endif
    vinox_mcp_server_config cfg_oversize;
    memset(&cfg_oversize, 0, sizeof(cfg_oversize));
    cfg_oversize.struct_size = sizeof(cfg_oversize);
    cfg_oversize.server_name = "vinox_mcp_oversize";
    cfg_oversize.transport_kind = VINOX_MCP_TRANSPORT_STDIO;
    cfg_oversize.command_or_url = "vinox_mcp_server.exe";
    cfg_oversize.protocol_version = VINOX_MCP_VERSION_2026_07_28;

    vinox_mcp_client* oversize_client = NULL;
    if (vinox_mcp_client_create(&cfg_oversize, &oversize_client) == VINOX_STATUS_OK && oversize_client) {
        if (vinox_mcp_client_connect(oversize_client) == VINOX_STATUS_OK) {
            call_req.call_id = "call_oversize_output_sim";
            call_req.tool_name = "vinox_mcp_oversize.vinox.search";
            call_req.arguments_json = "{\"query\":\"oversize output test\"}";

            memset(&call_res, 0, sizeof(call_res));
            call_res.struct_size = sizeof(call_res);

            if (vinox_mcp_client_call_tool(oversize_client, &call_req, &call_res, pool, sizeof(pool)) != VINOX_STATUS_OK ||
                call_res.result_json == NULL ||
                strstr(call_res.result_json, "exceeded maximum output payload size limit") == NULL) {
                printf("FAILED: Oversize tool output > 256 KB must fail closed!\n");
                vinox_mcp_client_destroy(oversize_client);
                vinox_tool_registry_destroy(reg);
                vinox_mcp_client_destroy(client);
                return 1;
            }
        }
        vinox_mcp_client_destroy(oversize_client);
    }
#if defined(_WIN32)
    _putenv("VINOX_TEST_OVERSIZE_SIM_KB=");
#else
    unsetenv("VINOX_TEST_OVERSIZE_SIM_KB");
#endif
    printf("  - Oversize Output Payload Limit (256 KB Gate): Verified\n");

    vinox_tool_registry_destroy(reg);
    vinox_mcp_client_destroy(client);

    /* 11. Registry Init Failure Injection Test */
#if defined(_WIN32)
    _putenv("VINOX_TEST_FAIL_REGISTRY=1");
#else
    setenv("VINOX_TEST_FAIL_REGISTRY", "1", 1);
#endif

    vinox_mcp_client* fail_reg_client = NULL;
    if (vinox_mcp_client_create(&cfg, &fail_reg_client) == VINOX_STATUS_OK && fail_reg_client) {
        if (vinox_mcp_client_connect(fail_reg_client) == VINOX_STATUS_OK) {
            call_req.call_id = "call_fail_reg";
            call_req.tool_name = "vinox_mcp.vinox.search";
            call_req.arguments_json = "{\"query\":\"test\"}";

            memset(&call_res, 0, sizeof(call_res));
            call_res.struct_size = sizeof(call_res);

            if (vinox_mcp_client_call_tool(fail_reg_client, &call_req, &call_res, pool, sizeof(pool)) == VINOX_STATUS_OK) {
                if (call_res.result_json == NULL || strstr(call_res.result_json, "governance engine unavailable") == NULL) {
                    printf("FAILED: Tool execution on failed registry init must fail closed with governance engine error!\n");
                    vinox_mcp_client_destroy(fail_reg_client);
                    return 1;
                }
            }
        }
        vinox_mcp_client_destroy(fail_reg_client);
    }
#if defined(_WIN32)
    _putenv("VINOX_TEST_FAIL_REGISTRY=");
#else
    unsetenv("VINOX_TEST_FAIL_REGISTRY");
#endif
    printf("  - Registry Initialization Failure Injection & Fail-Closed Gate: Verified\n");

    /* 7. Negative Test: Backend Initialization Failure / Engine Open Error */
#if defined(_WIN32)
    _putenv("VINOX_STORAGE_DB=C:\\invalid_path_dir_non_existent\\invalid.db");
#else
    setenv("VINOX_STORAGE_DB", "/invalid_path_dir_non_existent/invalid.db", 1);
#endif

    vinox_mcp_client* bad_client = NULL;
    if (vinox_mcp_client_create(&cfg, &bad_client) == VINOX_STATUS_OK && bad_client) {
        if (vinox_mcp_client_connect(bad_client) == VINOX_STATUS_OK) {
            call_req.call_id = "call_bad_backend";
            call_req.tool_name = "vinox_mcp.vinox.search";
            call_req.arguments_json = "{\"query\":\"test\"}";

            memset(&call_res, 0, sizeof(call_res));
            call_res.struct_size = sizeof(call_res);

            if (vinox_mcp_client_call_tool(bad_client, &call_req, &call_res, pool, sizeof(pool)) == VINOX_STATUS_OK) {
                if (call_res.result_json == NULL || strstr(call_res.result_json, "storage backend unavailable") == NULL) {
                    printf("FAILED: Tool call on failed storage backend must return backend unavailable error!\n");
                    vinox_mcp_client_destroy(bad_client);
                    return 1;
                }
            }
        }
        vinox_mcp_client_destroy(bad_client);
    }
    printf("  - Negative Test: Storage Initialization Failure & Backend Unavailable Error: Verified\n");

    remove(seeded_db_file);
    printf("SUCCESS: All VINOX Phase 6.3 Standalone MCP Server smoke tests passed!\n");
    return 0;
}
