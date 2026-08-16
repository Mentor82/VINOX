#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vinox/tools.h"
#include "vinox/vinox.h"

int main(void) {
    printf("Starting VINOX Phase 6.1 Tool Registry & Policy Engine Smoke Test...\n");

    // 1. Create Tool Registry
    vinox_tool_registry* registry = NULL;
    if (vinox_tool_registry_create(&registry) != VINOX_STATUS_OK || !registry) {
        printf("FAILED: vinox_tool_registry_create\n");
        return 1;
    }

    // 2. Register Tools
    const char* search_schema = "{"
        "\"type\":\"object\","
        "\"properties\":{"
            "\"query\":{\"type\":\"string\"},"
            "\"limit\":{\"type\":\"integer\"}"
        "},"
        "\"required\":[\"query\"]"
    "}";

    vinox_tool_definition tool_search;
    memset(&tool_search, 0, sizeof(tool_search));
    tool_search.struct_size = sizeof(tool_search);
    tool_search.name = "vinox.search";
    tool_search.description = "Hybrid BM25 + Vector retrieval search";
    tool_search.parameters_json_schema = search_schema;
    tool_search.security_class = VINOX_SECURITY_CLASS_READ_ONLY;

    if (vinox_tool_registry_register_tool(registry, &tool_search) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_tool_registry_register_tool (vinox.search): %s\n", vinox_tools_last_error());
        vinox_tool_registry_destroy(registry);
        return 2;
    }

    vinox_tool_definition tool_admin;
    memset(&tool_admin, 0, sizeof(tool_admin));
    tool_admin.struct_size = sizeof(tool_admin);
    tool_admin.name = "sys.admin_exec";
    tool_admin.description = "System administrator command execution";
    tool_admin.parameters_json_schema = "{}";
    tool_admin.security_class = VINOX_SECURITY_CLASS_ADMIN;

    if (vinox_tool_registry_register_tool(registry, &tool_admin) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_tool_registry_register_tool (sys.admin_exec)\n");
        vinox_tool_registry_destroy(registry);
        return 3;
    }

    // 3. Find Tool & Verify Pool Buffer
    vinox_tool_definition found_def;
    memset(&found_def, 0, sizeof(found_def));
    found_def.struct_size = sizeof(found_def);
    char pool[1024];

    if (vinox_tool_registry_find_tool(registry, "vinox.search", &found_def, pool, sizeof(pool)) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_tool_registry_find_tool\n");
        vinox_tool_registry_destroy(registry);
        return 4;
    }

    if (strcmp(found_def.name, "vinox.search") != 0 || found_def.security_class != VINOX_SECURITY_CLASS_READ_ONLY) {
        printf("FAILED: Found tool definition properties mismatch\n");
        vinox_tool_registry_destroy(registry);
        return 5;
    }

    // 4. Validate Arguments against Schema
    char err_buf[512];

    // Valid args
    if (vinox_tool_registry_validate_arguments(registry, "vinox.search", "{\"query\":\"openvino\", \"limit\":5}", err_buf, sizeof(err_buf)) != VINOX_STATUS_OK) {
        printf("FAILED: Argument validation for valid args\n");
        vinox_tool_registry_destroy(registry);
        return 6;
    }

    // Missing required field 'query'
    if (vinox_tool_registry_validate_arguments(registry, "vinox.search", "{\"limit\":5}", err_buf, sizeof(err_buf)) != VINOX_STATUS_INVALID_ARGUMENT ||
        strstr(err_buf, "Missing required parameter") == NULL) {
        printf("FAILED: Argument validation failed to catch missing required 'query'\n");
        vinox_tool_registry_destroy(registry);
        return 7;
    }

    // Parameter type mismatch (string passed for integer 'limit')
    if (vinox_tool_registry_validate_arguments(registry, "vinox.search", "{\"query\":\"test\", \"limit\":\"five\"}", err_buf, sizeof(err_buf)) != VINOX_STATUS_INVALID_ARGUMENT ||
        strstr(err_buf, "expected type") == NULL) {
        printf("FAILED: Argument validation failed to catch type mismatch for 'limit'\n");
        vinox_tool_registry_destroy(registry);
        return 8;
    }

    // 5. Policy Engine Evaluation
    vinox_policy_engine* policy = NULL;
    if (vinox_policy_engine_create(&policy) != VINOX_STATUS_OK || !policy) {
        printf("FAILED: vinox_policy_engine_create\n");
        vinox_tool_registry_destroy(registry);
        return 9;
    }

    // Set rule: vinox.* allowed up to READ_ONLY
    if (vinox_policy_engine_set_rule(policy, "vinox.*", VINOX_SECURITY_CLASS_READ_ONLY, VINOX_APPROVAL_AUTO_ALLOWED) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_policy_engine_set_rule\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 10;
    }

    vinox_tool_call_request req_search;
    memset(&req_search, 0, sizeof(req_search));
    req_search.struct_size = sizeof(req_search);
    req_search.call_id = "call_search_001";
    req_search.tool_name = "vinox.search";
    req_search.arguments_json = "{\"query\":\"test\"}";

    vinox_policy_decision dec_search;
    memset(&dec_search, 0, sizeof(dec_search));
    dec_search.struct_size = sizeof(dec_search);
    char reason_buf[256];

    if (vinox_policy_engine_evaluate(policy, &req_search, &tool_search, &dec_search, reason_buf, sizeof(reason_buf)) != VINOX_STATUS_OK ||
        dec_search.allowed != 1 || dec_search.approval_mode != VINOX_APPROVAL_AUTO_ALLOWED) {
        printf("FAILED: Policy engine failed to allow vinox.search\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 11;
    }

    // Evaluate sys.admin_exec (ADMIN tier vs READ_ONLY max allowed rule) -> must be denied
    vinox_tool_call_request req_admin;
    memset(&req_admin, 0, sizeof(req_admin));
    req_admin.struct_size = sizeof(req_admin);
    req_admin.call_id = "call_admin_002";
    req_admin.tool_name = "sys.admin_exec";
    req_admin.arguments_json = "{}";

    vinox_policy_decision dec_admin;
    memset(&dec_admin, 0, sizeof(dec_admin));
    dec_admin.struct_size = sizeof(dec_admin);

    if (vinox_policy_engine_evaluate(policy, &req_admin, &tool_admin, &dec_admin, reason_buf, sizeof(reason_buf)) != VINOX_STATUS_OK ||
        dec_admin.allowed != 0) {
        printf("FAILED: Policy engine failed to deny sys.admin_exec\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 12;
    }

    // 6. OpenAI Format Mapping
    char openai_schema_buf[2048];
    size_t req_sz = 0;
    if (vinox_tools_format_openai_schema(registry, openai_schema_buf, sizeof(openai_schema_buf), &req_sz) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_tools_format_openai_schema\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 13;
    }

    if (strstr(openai_schema_buf, "\"name\":\"vinox.search\"") == NULL || strstr(openai_schema_buf, "\"type\":\"function\"") == NULL) {
        printf("FAILED: OpenAI schema format verification (got '%s')\n", openai_schema_buf);
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 14;
    }

    // Parse OpenAI Tool Call JSON
    const char* openai_call_json = "{"
        "\"id\":\"call_openai_999\","
        "\"type\":\"function\","
        "\"function\":{"
            "\"name\":\"vinox.search\","
            "\"arguments\":\"{\\\"query\\\":\\\"openvino genai\\\"}\""
        "}"
    "}";

    vinox_tool_call_request parsed_req;
    memset(&parsed_req, 0, sizeof(parsed_req));
    parsed_req.struct_size = sizeof(parsed_req);

    if (vinox_tools_parse_openai_tool_call(openai_call_json, &parsed_req, pool, sizeof(pool)) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_tools_parse_openai_tool_call\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 15;
    }

    if (strcmp(parsed_req.call_id, "call_openai_999") != 0 || strcmp(parsed_req.tool_name, "vinox.search") != 0) {
        printf("FAILED: Parsed OpenAI tool call values mismatch\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 16;
    }

    vinox_policy_engine_destroy(policy);
    vinox_tool_registry_destroy(registry);

    printf("SUCCESS: All VINOX Phase 6.1 Tool Registry & Policy Engine smoke tests passed!\n");
    return 0;
}
