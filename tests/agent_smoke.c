#include "vinox/vinox_agent.h"
#include "vinox/tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("Starting VINOX Phase 7 Real Agent Step Execution & Governance Smoke Test...\n");

    /* 1. Mode Controller Invariants */
    vinox_mode_controller* controller = vinox_mode_controller_create();
    if (!controller) {
        printf("FAILED: vinox_mode_controller_create returned NULL\n");
        return 1;
    }

    if (vinox_mode_controller_get_mode(controller) != VINOX_MODE_CHAT) {
        printf("FAILED: Default mode must be VINOX_MODE_CHAT\n");
        return 1;
    }

    if (vinox_mode_controller_can_execute_mutating_tool(controller) != 0) {
        printf("FAILED: Mutating tool execution must be rejected in CHAT mode\n");
        return 1;
    }

    if (vinox_mode_controller_set_mode(controller, VINOX_MODE_PLAN) != VINOX_STATUS_OK) {
        printf("FAILED: Could not transition to PLAN mode\n");
        return 1;
    }

    if (vinox_mode_controller_can_execute_mutating_tool(controller) != 0) {
        printf("FAILED: Mutating tool execution must be rejected in PLAN mode\n");
        return 1;
    }

    if (vinox_mode_controller_set_mode(controller, VINOX_MODE_AGENT) != VINOX_STATUS_OK) {
        printf("FAILED: Could not transition to AGENT mode\n");
        return 1;
    }

    if (vinox_mode_controller_can_execute_mutating_tool(controller) != 1) {
        printf("FAILED: Mutating tool execution must be allowed in AGENT mode\n");
        return 1;
    }
    printf("  [PASS 01] Mode Controller Immutable Policy Invariants: Verified\n");

    /* 2. Setup Phase 6 Governance Tool Registry & Policy Engine */
    vinox_tool_registry* registry = NULL;
    if (vinox_tool_registry_create(&registry) != VINOX_STATUS_OK || !registry) {
        printf("FAILED: Failed to create tool registry\n");
        return 1;
    }

    vinox_policy_engine* policy_engine = NULL;
    if (vinox_policy_engine_create(&policy_engine) != VINOX_STATUS_OK || !policy_engine) {
        printf("FAILED: Failed to create policy engine\n");
        return 1;
    }
    vinox_policy_engine_set_rule(policy_engine, "vinox.*", VINOX_SECURITY_CLASS_READ_ONLY, VINOX_APPROVAL_AUTO_ALLOWED);

    vinox_tool_definition search_tool;
    memset(&search_tool, 0, sizeof(search_tool));
    search_tool.struct_size = sizeof(search_tool);
    search_tool.name = "vinox.search";
    search_tool.description = "Search VINOX memory";
    search_tool.parameters_json_schema = "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},\"required\":[\"query\"],\"additionalProperties\":false}";
    search_tool.security_class = VINOX_SECURITY_CLASS_READ_ONLY;
    vinox_tool_registry_register_tool(registry, &search_tool);

    /* 3. Real Multi-Step Dependency Graph Plan */
    const char* multi_step_plan_json =
        "{"
        "  \"goal\": \"Multi-step dependency resolution test\","
        "  \"steps\": ["
        "    {\"step_id\": \"step_1\", \"description\": \"Inspect memory\", \"tool_calls\": [{\"name\": \"vinox.search\", \"arguments\": {\"query\": \"VINOX\"}}]},"
        "    {\"step_id\": \"step_2\", \"description\": \"Process search results\", \"dependencies\": [\"step_1\"]}"
        "  ]"
        "}";

    vinox_plan* plan = vinox_plan_create(multi_step_plan_json);
    if (!plan) {
        printf("FAILED: vinox_plan_create returned NULL\n");
        return 1;
    }

    char plan_hash[65] = {0};
    vinox_plan_compute_hash(plan, plan_hash, sizeof(plan_hash));
    vinox_plan_approve(plan, plan_hash);

    /* 4. Real Agent Step Execution with Governance Gate & Dependency Resolution */
    vinox_agent_budget budget;
    memset(&budget, 0, sizeof(budget));
    budget.struct_size = sizeof(budget);
    budget.max_steps = 10;
    budget.max_tokens = 4096;
    budget.max_tool_calls = 10;
    budget.max_duration_seconds = 60;

    vinox_agent_run* run = vinox_agent_run_create(controller, plan, &budget);
    if (!run) {
        printf("FAILED: vinox_agent_run_create returned NULL\n");
        return 1;
    }
    vinox_agent_run_set_governance(run, registry, policy_engine);

    /* Step 1 Execution (vinox.search evaluated via Phase 6 Registry/Policy) */
    if (vinox_agent_run_step(run) != VINOX_STATUS_OK) {
        printf("FAILED: Real agent step 1 execution failed!\n");
        return 1;
    }

    if (vinox_agent_run_get_completed_steps(run) != 1) {
        printf("FAILED: Step 1 completed count mismatch\n");
        return 1;
    }

    /* Step 2 Execution (Dependency step_1 satisfied) */
    if (vinox_agent_run_step(run) != VINOX_STATUS_OK) {
        printf("FAILED: Real agent step 2 execution failed!\n");
        return 1;
    }

    if (vinox_agent_run_get_completed_steps(run) != 2) {
        printf("FAILED: Step 2 completed count mismatch\n");
        return 1;
    }
    printf("  [PASS 02] Real Multi-Step Dependency Execution & Phase 6 Governance Gate: Verified\n");

    /* Cleanup */
    vinox_agent_run_destroy(run);
    vinox_plan_destroy(plan);
    vinox_policy_engine_destroy(policy_engine);
    vinox_tool_registry_destroy(registry);
    vinox_mode_controller_destroy(controller);

    printf("SUCCESS: All VINOX Phase 7 Real Agent Engine & Governance smoke tests passed!\n");
    return 0;
}
