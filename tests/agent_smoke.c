#include "vinox/vinox_agent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("Starting VINOX Phase 7 Agent Engine & Mode Controller Smoke Test...\n");

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
    printf("  - Mode Controller Immutable Policies: Verified\n");

    /* 2. Plan Schema & Hash Binding */
    const char* plan_json =
        "{"
        "  \"goal\": \"Add logging utility to core\","
        "  \"steps\": ["
        "    {\"step_id\": \"step_1\", \"description\": \"Inspect files\"},"
        "    {\"step_id\": \"step_2\", \"description\": \"Write code\"}"
        "  ]"
        "}";

    vinox_plan* plan = vinox_plan_create(plan_json);
    if (!plan) {
        printf("FAILED: vinox_plan_create returned NULL for valid JSON\n");
        return 1;
    }

    if (vinox_plan_validate(plan) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_plan_validate failed for valid plan\n");
        return 1;
    }

    char plan_hash[65] = {0};
    if (vinox_plan_compute_hash(plan, plan_hash, sizeof(plan_hash)) != VINOX_STATUS_OK || strlen(plan_hash) != 64) {
        printf("FAILED: vinox_plan_compute_hash failed\n");
        return 1;
    }

    /* Hash mismatch rejection */
    if (vinox_plan_approve(plan, "invalid_hash_12345678901234567890123456789012345678901234567890123456") == VINOX_STATUS_OK) {
        printf("FAILED: vinox_plan_approve must fail on hash mismatch\n");
        return 1;
    }

    /* Valid approval */
    if (vinox_plan_approve(plan, plan_hash) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_plan_approve failed for matching SHA-256 hash\n");
        return 1;
    }

    if (vinox_plan_get_status(plan) != VINOX_PLAN_STATUS_APPROVED) {
        printf("FAILED: Plan status must be APPROVED\n");
        return 1;
    }
    printf("  - Plan Model Validation & SHA-256 Cryptographic Binding: Verified\n");

    /* 3. Agent Execution Loop & Budget Enforcement */
    vinox_agent_budget budget;
    memset(&budget, 0, sizeof(budget));
    budget.struct_size = sizeof(budget);
    budget.max_steps = 2;
    budget.max_tokens = 2048;
    budget.max_tool_calls = 5;
    budget.max_duration_seconds = 60;

    vinox_agent_run* run = vinox_agent_run_create(controller, plan, &budget);
    if (!run) {
        printf("FAILED: vinox_agent_run_create returned NULL\n");
        return 1;
    }

    if (vinox_agent_run_step(run) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_agent_run_step 1 failed\n");
        return 1;
    }

    if (vinox_agent_run_get_completed_steps(run) != 1) {
        printf("FAILED: Completed steps count mismatch\n");
        return 1;
    }

    if (vinox_agent_run_step(run) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_agent_run_step 2 failed\n");
        return 1;
    }

    /* Step budget limit enforcement */
    if (vinox_agent_run_step(run) == VINOX_STATUS_OK) {
        printf("FAILED: vinox_agent_run_step must fail when max_steps budget is exceeded\n");
        return 1;
    }
    printf("  - Agent Orchestration Loop & Step/Tool Budget Enforcement: Verified\n");

    /* Cleanup */
    vinox_agent_run_destroy(run);
    vinox_plan_destroy(plan);
    vinox_mode_controller_destroy(controller);

    printf("SUCCESS: All VINOX Phase 7 Agent Engine & Mode Controller smoke tests passed!\n");
    return 0;
}
