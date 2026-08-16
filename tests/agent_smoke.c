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
    vinox_policy_engine_set_rule(policy_engine, "local_write.*", VINOX_SECURITY_CLASS_LOCAL_WRITE, VINOX_APPROVAL_AUTO_ALLOWED);

    vinox_tool_definition write_tool;
    memset(&write_tool, 0, sizeof(write_tool));
    write_tool.struct_size = sizeof(write_tool);
    write_tool.name = "local_write.write";
    write_tool.description = "Write file in sandbox";
    write_tool.parameters_json_schema = "{\"type\":\"object\",\"properties\":{\"filename\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"filename\",\"content\"],\"additionalProperties\":false}";
    write_tool.security_class = VINOX_SECURITY_CLASS_LOCAL_WRITE;
    vinox_tool_registry_register_tool(registry, &write_tool);

    /* 3. Setup Sandbox Host */
    const char* overlay_dir = ".agent_test_overlay";
    vinox_sandbox_host* sandbox_host = vinox_sandbox_host_create(overlay_dir);
    if (!sandbox_host || vinox_sandbox_host_start(sandbox_host, "vinox_sandbox_worker.exe") != VINOX_STATUS_OK) {
        printf("FAILED: vinox_sandbox_host_start failed\n");
        return 1;
    }

    /* 4. Real Multi-Step Dependency Graph Plan */
    const char* multi_step_plan_json =
        "{"
        "  \"goal\": \"Multi-step dependency resolution test\","
        "  \"steps\": ["
        "    {\"step_id\": \"step_1\", \"description\": \"Write artifact in sandbox\", \"tool_calls\": [{\"name\": \"local_write.write\", \"arguments\": {\"filename\": \"agent_out.txt\", \"content\": \"Agent Sandbox Success!\"}}]},"
        "    {\"step_id\": \"step_2\", \"description\": \"Finalize step\", \"dependencies\": [\"step_1\"]}"
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

    /* 5. Fail-Closed Test: Step Execution FAILS if Governance is Missing */
    vinox_agent_budget budget;
    memset(&budget, 0, sizeof(budget));
    budget.struct_size = sizeof(budget);
    budget.max_steps = 10;
    budget.max_tokens = 4096;
    budget.max_tool_calls = 10;
    budget.max_duration_seconds = 60;

    vinox_agent_run* ungov_run = vinox_agent_run_create(controller, plan, &budget);
    if (!ungov_run) {
        printf("FAILED: vinox_agent_run_create returned NULL\n");
        return 1;
    }

    if (vinox_agent_run_step(ungov_run) != VINOX_STATUS_PERMISSION_DENIED) {
        printf("FAILED: Step with tool call MUST fail closed if governance is missing!\n");
        return 1;
    }
    vinox_agent_run_destroy(ungov_run);
    printf("  [PASS 02] Missing Governance Fail-Closed Gate (PERMISSION_DENIED): Verified\n");

    /* 6. Real Agent Step Execution with Mandatory Governance & ACTUAL Sandbox Tool Execution */
    vinox_agent_run* run = vinox_agent_run_create(controller, plan, &budget);
    if (!run) {
        printf("FAILED: vinox_agent_run_create returned NULL\n");
        return 1;
    }
    vinox_agent_run_set_governance(run, registry, policy_engine);
    vinox_agent_run_set_sandbox(run, sandbox_host);

    /* Step 1 Execution (local_write.write evaluated via Governance & ACTUAL Sandbox Execution) */
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
    printf("  [PASS 03] Real Multi-Step Execution, Mandatory Governance & Actual Sandbox Dispatch: Verified\n");

    /* 7. Plan Extraction Fail-Closed on Run Creation */
    const char* empty_steps_plan_json = "{\"goal\":\"Invalid\",\"steps\":[]}";
    vinox_plan* empty_plan = vinox_plan_create(empty_steps_plan_json);
    if (empty_plan) {
        vinox_plan_compute_hash(empty_plan, plan_hash, sizeof(plan_hash));
        vinox_plan_approve(empty_plan, plan_hash);
        vinox_agent_run* empty_run = vinox_agent_run_create(controller, empty_plan, &budget);
        if (empty_run != NULL) {
            printf("FAILED: vinox_agent_run_create must fail closed on empty plan steps!\n");
            return 1;
        }
        vinox_plan_destroy(empty_plan);
    }
    printf("  [PASS 04] Empty Plan Extraction Fail-Closed Rejection: Verified\n");

    /* Cleanup */
    vinox_agent_run_destroy(run);
    vinox_plan_destroy(plan);
    vinox_sandbox_host_stop(sandbox_host);
    vinox_sandbox_host_destroy(sandbox_host);
    vinox_policy_engine_destroy(policy_engine);
    vinox_tool_registry_destroy(registry);
    vinox_mode_controller_destroy(controller);

    printf("SUCCESS: All VINOX Phase 7 Real Agent Engine & Governance smoke tests passed!\n");
    return 0;
}
