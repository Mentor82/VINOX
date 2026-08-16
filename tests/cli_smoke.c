#include "vinox/vinox_agent.h"
#include "vinox/tools.h"
#include "vinox/storage.h"
#include "vinox/vinox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

int main(void) {
    printf("Starting VINOX Phase 8 System E2E & CLI Reference Smoke Test...\n");

    const char* overlay_dir = ".cli_e2e_overlay";
    const char* target_dir = ".cli_e2e_target";

    /* 1. Mode Controller Invariant Check */
    vinox_mode_controller* controller = vinox_mode_controller_create();
    if (!controller) {
        printf("FAILED: vinox_mode_controller_create returned NULL\n");
        return 1;
    }

    if (vinox_mode_controller_set_mode(controller, VINOX_MODE_PLAN) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_mode_controller_set_mode PLAN failed\n");
        return 1;
    }
    printf("  [PASS 01] Mode Controller Transition to PLAN mode: Verified\n");

    /* 2. Setup Phase 6 Governance Tool Registry & Policy Engine */
    vinox_tool_registry* registry = NULL;
    vinox_tool_registry_create(&registry);

    vinox_policy_engine* policy_engine = NULL;
    vinox_policy_engine_create(&policy_engine);
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
    vinox_sandbox_host* sandbox_host = vinox_sandbox_host_create(overlay_dir);
    if (!sandbox_host || vinox_sandbox_host_start(sandbox_host, "vinox_sandbox_worker.exe") != VINOX_STATUS_OK) {
        printf("FAILED: vinox_sandbox_host_start failed\n");
        return 1;
    }
    printf("  [PASS 02] Sandbox Subprocess Worker Launch & Job Object Binding: Verified\n");

    /* 4. Create & Validate Plan Draft */
    const char* plan_json =
        "{"
        "  \"goal\": \"System E2E Verification Workflow\","
        "  \"steps\": ["
        "    {\"step_id\": \"step_1\", \"description\": \"Write artifact in sandbox\", \"tool_calls\": [{\"name\": \"local_write.write\", \"arguments\": {\"filename\": \"e2e_artifact.txt\", \"content\": \"VINOX E2E Success!\"}}]},"
        "    {\"step_id\": \"step_2\", \"description\": \"Finalize step\", \"dependencies\": [\"step_1\"]}"
        "  ]"
        "}";

    vinox_plan* plan = vinox_plan_create(plan_json);
    if (!plan || vinox_plan_validate(plan) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_plan_create or vinox_plan_validate failed\n");
        return 1;
    }

    char plan_hash[65] = {0};
    vinox_plan_compute_hash(plan, plan_hash, sizeof(plan_hash));
    if (vinox_plan_approve(plan, plan_hash) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_plan_approve failed\n");
        return 1;
    }
    printf("  [PASS 03] Plan Draft Creation, DAG Validation & Cryptographic Approval: Verified (Hash: %.16s...)\n", plan_hash);

    /* 5. Start Agent Run & Execute All Steps */
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
    vinox_agent_run_set_sandbox(run, sandbox_host);

    if (vinox_agent_run_step(run) != VINOX_STATUS_OK || vinox_agent_run_step(run) != VINOX_STATUS_OK) {
        printf("FAILED: Agent run step execution failed\n");
        return 1;
    }
    printf("  [PASS 04] Real Agent Step Dispatch & Sandbox Worker Execution: Verified\n");

    /* 6. Compute Truthful Diff & Target Snapshot Hash */
    char diff_buf[2048] = {0};
    if (vinox_artifact_commit_diff(overlay_dir, target_dir, diff_buf, sizeof(diff_buf)) != VINOX_STATUS_OK ||
        strstr(diff_buf, "SNAPSHOT:") == NULL) {
        printf("FAILED: vinox_artifact_commit_diff failed\n");
        return 1;
    }

    char snapshot_hash[65] = {0};
    const char* snap_ptr = strstr(diff_buf, "SNAPSHOT:");
    if (snap_ptr) {
        sscanf(snap_ptr, "SNAPSHOT:%64s", snapshot_hash);
    }
    printf("  [PASS 05] Truthful Artifact Unified Diff & Target Snapshot Calculation: Verified\n");

    /* 7. Apply Takeover with Snapshot Binding */
    if (vinox_artifact_commit_apply_snapshot(overlay_dir, target_dir, snapshot_hash) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_artifact_commit_apply_snapshot failed\n");
        return 1;
    }

    /* Verify file exists in target workspace disk and content is accurate */
    FILE* f = fopen(".cli_e2e_target/e2e_artifact.txt", "rb");
    if (!f) {
        printf("FAILED: Artifact file does not exist in target workspace!\n");
        return 1;
    }
    char file_content[128] = {0};
    fread(file_content, 1, sizeof(file_content) - 1, f);
    fclose(f);

    if (strstr(file_content, "VINOX E2E Success!") == NULL) {
        printf("FAILED: Committed file content mismatch: %s\n", file_content);
        return 1;
    }
    printf("  [PASS 06] Snapshot-Bound Atomic Takeover & Real Workspace File Verification: Verified\n");

    /* Cleanup */
    vinox_agent_run_destroy(run);
    vinox_plan_destroy(plan);
    vinox_sandbox_host_stop(sandbox_host);
    vinox_sandbox_host_destroy(sandbox_host);
    vinox_policy_engine_destroy(policy_engine);
    vinox_tool_registry_destroy(registry);
    vinox_mode_controller_destroy(controller);

    printf("SUCCESS: All VINOX Phase 8 System E2E & CLI Reference smoke tests passed! 🟢🔒\n");
    return 0;
}
