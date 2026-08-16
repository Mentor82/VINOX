#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vinox/tools.h"
#include "vinox/vinox.h"

int main(void) {
    printf("Starting VINOX Issue #9 Hardened Tool Registry & Policy Engine Smoke Test...\n");

    // 1. Create Tool Registry
    vinox_tool_registry* registry = NULL;
    if (vinox_tool_registry_create(&registry) != VINOX_STATUS_OK || !registry) {
        printf("FAILED: vinox_tool_registry_create\n");
        return 1;
    }

    // 2. Register Tools (including additionalProperties: false & enum property specs)
    const char* search_schema = "{"
        "\"type\":\"object\","
        "\"properties\":{"
            "\"query\":{\"type\":\"string\"},"
            "\"limit\":{\"type\":\"integer\"},"
            "\"mode\":{\"type\":\"string\",\"enum\":[\"fast\",\"precise\"]}"
        "},"
        "\"required\":[\"query\"],"
        "\"additionalProperties\":false"
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

    // 3. Pool Exhaustion Safety & Prefix-ABI Find Tool Test
    char tiny_pool[5];
    vinox_tool_definition unmutated_def;
    memset(&unmutated_def, 0xAB, sizeof(unmutated_def));
    unmutated_def.struct_size = sizeof(unmutated_def);

    if (vinox_tool_registry_find_tool(registry, "vinox.search", &unmutated_def, tiny_pool, sizeof(tiny_pool)) != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: vinox_tool_registry_find_tool failed to reject tiny_pool\n");
        vinox_tool_registry_destroy(registry);
        return 4;
    }

    char pool[1024];
    vinox_tool_definition found_def;
    memset(&found_def, 0, sizeof(found_def));
    found_def.struct_size = VINOX_TOOL_DEFINITION_MIN_SIZE;

    if (vinox_tool_registry_find_tool(registry, "vinox.search", &found_def, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        strcmp(found_def.name, "vinox.search") != 0) {
        printf("FAILED: vinox_tool_registry_find_tool prefix ABI find\n");
        vinox_tool_registry_destroy(registry);
        return 5;
    }

    // 4. Schema Validation Hardening (additionalProperties: false & enum validation)
    char err_buf[512];

    // Valid args
    if (vinox_tool_registry_validate_arguments(registry, "vinox.search", "{\"query\":\"openvino\",\"mode\":\"fast\"}", err_buf, sizeof(err_buf)) != VINOX_STATUS_OK) {
        printf("FAILED: Valid argument validation\n");
        vinox_tool_registry_destroy(registry);
        return 6;
    }

    // Additional property forbidden test
    if (vinox_tool_registry_validate_arguments(registry, "vinox.search", "{\"query\":\"test\",\"unsupported_arg\":\"x\"}", err_buf, sizeof(err_buf)) != VINOX_STATUS_INVALID_ARGUMENT ||
        strstr(err_buf, "Additional property") == NULL) {
        printf("FAILED: additionalProperties: false check failed (got '%s')\n", err_buf);
        vinox_tool_registry_destroy(registry);
        return 7;
    }

    // Enum value mismatch test
    if (vinox_tool_registry_validate_arguments(registry, "vinox.search", "{\"query\":\"test\",\"mode\":\"invalid_mode\"}", err_buf, sizeof(err_buf)) != VINOX_STATUS_INVALID_ARGUMENT ||
        strstr(err_buf, "allowed enum values") == NULL) {
        printf("FAILED: Enum parameter validation failed (got '%s')\n", err_buf);
        vinox_tool_registry_destroy(registry);
        return 8;
    }

    // 5. Policy Engine Hardening (Default-Deny, Range Validation & Request/Tool Mismatch)
    vinox_policy_engine* policy = NULL;
    if (vinox_policy_engine_create(&policy) != VINOX_STATUS_OK || !policy) {
        printf("FAILED: vinox_policy_engine_create\n");
        vinox_tool_registry_destroy(registry);
        return 9;
    }

    vinox_tool_call_request req_search;
    memset(&req_search, 0, sizeof(req_search));
    req_search.struct_size = sizeof(req_search);
    req_search.call_id = "call_search_001";
    req_search.tool_name = "vinox.search";
    req_search.arguments_json = "{\"query\":\"test\"}";

    vinox_policy_decision decision;
    memset(&decision, 0, sizeof(decision));
    decision.struct_size = sizeof(decision);
    char reason_buf[256];

    // TEST: Empty Policy Engine MUST be 100% Default-Deny (even for READ_ONLY tool)
    if (vinox_policy_engine_evaluate(policy, &req_search, &tool_search, &decision, reason_buf, sizeof(reason_buf)) != VINOX_STATUS_OK ||
        decision.allowed != 0 || decision.approval_mode != VINOX_APPROVAL_DENIED) {
        printf("FAILED: Empty policy engine was not default-deny! (allowed=%d)\n", decision.allowed);
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 10;
    }

    // TEST: Policy Rule Enum Range Validation Rejection
    if (vinox_policy_engine_set_rule(policy, "vinox.*", VINOX_SECURITY_CLASS_READ_ONLY, 999) != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: vinox_policy_engine_set_rule failed to reject invalid approval_mode=999\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 11;
    }

    if (vinox_policy_engine_set_rule(policy, "vinox.*", 999, VINOX_APPROVAL_AUTO_ALLOWED) != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: vinox_policy_engine_set_rule failed to reject invalid max_security_class=999\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 12;
    }

    // Configure valid allow rule for vinox.*
    if (vinox_policy_engine_set_rule(policy, "vinox.*", VINOX_SECURITY_CLASS_READ_ONLY, VINOX_APPROVAL_AUTO_ALLOWED) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_policy_engine_set_rule\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 13;
    }

    // TEST: Valid Policy Evaluation after setting allow rule
    if (vinox_policy_engine_evaluate(policy, &req_search, &tool_search, &decision, reason_buf, sizeof(reason_buf)) != VINOX_STATUS_OK ||
        decision.allowed != 1 || decision.approval_mode != VINOX_APPROVAL_AUTO_ALLOWED) {
        printf("FAILED: Policy engine evaluation failed for vinox.search\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 14;
    }

    // TEST: Request / Tool Definition Name Mismatch Rejection
    vinox_tool_call_request req_mismatch;
    memset(&req_mismatch, 0, sizeof(req_mismatch));
    req_mismatch.struct_size = sizeof(req_mismatch);
    req_mismatch.call_id = "call_mismatch_002";
    req_mismatch.tool_name = "vinox.search"; // requested tool name
    req_mismatch.arguments_json = "{}";

    // Pass tool_admin definition ("sys.admin_exec") -> must fail with INVALID_ARGUMENT and allowed = 0
    if (vinox_policy_engine_evaluate(policy, &req_mismatch, &tool_admin, &decision, reason_buf, sizeof(reason_buf)) != VINOX_STATUS_INVALID_ARGUMENT ||
        decision.allowed != 0 || strstr(reason_buf, "does not match") == NULL) {
        printf("FAILED: Policy engine failed to reject tool name mismatch\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 15;
    }

    // 6. OpenAI Format Mapping & Pool Capacity Verification
    char openai_schema_buf[2048];
    size_t req_sz = 0;
    if (vinox_tools_format_openai_schema(registry, openai_schema_buf, sizeof(openai_schema_buf), &req_sz) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_tools_format_openai_schema\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 16;
    }

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

    // Tiny pool parsing test
    if (vinox_tools_parse_openai_tool_call(openai_call_json, &parsed_req, tiny_pool, sizeof(tiny_pool)) != VINOX_STATUS_INVALID_ARGUMENT) {
        printf("FAILED: vinox_tools_parse_openai_tool_call failed to reject tiny_pool\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 17;
    }

    // Happy path parsing test
    if (vinox_tools_parse_openai_tool_call(openai_call_json, &parsed_req, pool, sizeof(pool)) != VINOX_STATUS_OK ||
        strcmp(parsed_req.call_id, "call_openai_999") != 0 || strcmp(parsed_req.tool_name, "vinox.search") != 0) {
        printf("FAILED: vinox_tools_parse_openai_tool_call happy path\n");
        vinox_policy_engine_destroy(policy);
        vinox_tool_registry_destroy(registry);
        return 18;
    }

    vinox_policy_engine_destroy(policy);
    vinox_tool_registry_destroy(registry);

    printf("SUCCESS: All VINOX Issue #9 Hardened Tool Registry & Policy Engine smoke tests passed!\n");
    return 0;
}
