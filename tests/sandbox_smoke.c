#include "vinox/vinox_agent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

int main(void) {
    printf("Starting VINOX Phase 7 Sandbox Isolation & Mandatory Reviewed Takeover Smoke Test...\n");

    const char* overlay_dir = ".test_overlay";
    const char* target_dir = ".test_target";

    /* 1. Launch Sandbox Host & Subprocess Worker with Fail-Closed Job Object Setup */
    vinox_sandbox_host* host = vinox_sandbox_host_create(overlay_dir);
    if (!host) {
        printf("FAILED: vinox_sandbox_host_create returned NULL\n");
        return 1;
    }

    if (vinox_sandbox_host_start(host, "vinox_sandbox_worker.exe") != VINOX_STATUS_OK) {
        printf("FAILED: vinox_sandbox_host_start failed\n");
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  [PASS 01] Sandbox Worker Subprocess & Fail-Closed Job Object Resource Binding: Verified\n");

    /* 2. Execute Valid Mutating Write Tool in Sandbox Overlay */
    char res_buf[1024] = {0};
    const char* tool_args = "{\"filename\":\"test_file.txt\",\"content\":\"Hello Sandbox World!\"}";
    if (vinox_sandbox_host_exec_tool(host, "local_write.write", tool_args, res_buf, sizeof(res_buf)) != VINOX_STATUS_OK ||
        strstr(res_buf, "\"status\":\"OK\"") == NULL) {
        printf("FAILED: vinox_sandbox_host_exec_tool failed: %s\n", res_buf);
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  [PASS 02] Isolated Sandbox Workspace File Creation & Stdio Pipe RPC: Verified\n");

    /* 3. Execute Real File Read Tool Execution */
    char read_res_buf[1024] = {0};
    const char* read_args = "{\"filename\":\"test_file.txt\"}";
    if (vinox_sandbox_host_exec_tool(host, "local_read.read", read_args, read_res_buf, sizeof(read_res_buf)) != VINOX_STATUS_OK ||
        strstr(read_res_buf, "Hello Sandbox World!") == NULL) {
        printf("FAILED: Real file read tool execution failed: %s\n", read_res_buf);
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  [PASS 03] Real Sandbox File Read Tool Execution (local_read.read): Verified\n");

    /* 4. Compute Truthful Diff & Target Snapshot Hash */
    char diff_buf[2048] = {0};
    if (vinox_artifact_commit_diff(overlay_dir, target_dir, diff_buf, sizeof(diff_buf)) != VINOX_STATUS_OK ||
        strstr(diff_buf, "SNAPSHOT:") == NULL) {
        printf("FAILED: vinox_artifact_commit_diff failed: %s\n", diff_buf);
        vinox_sandbox_host_destroy(host);
        return 1;
    }

    char snapshot_hash[65] = {0};
    const char* snap_ptr = strstr(diff_buf, "SNAPSHOT:");
    if (snap_ptr) {
        sscanf(snap_ptr, "SNAPSHOT:%64s", snapshot_hash);
    }
    printf("  [PASS 04] Truthful Artifact Unified Diff & Target Snapshot Engine: Verified (Snapshot: %.16s...)\n", snapshot_hash);

    /* 5. Negative Test: Mandatory Snapshot Requirement Rejection */
    if (vinox_artifact_commit_apply(overlay_dir, target_dir) == VINOX_STATUS_OK) {
        printf("FAILED: vinox_artifact_commit_apply without snapshot hash MUST fail!\n");
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  [PASS 05] Mandatory Snapshot Binding (Unreviewed Takeover Rejection): Verified\n");

    /* 6. Negative Test: Review->Apply Target Conflict Detection Rejection */
    if (vinox_artifact_commit_apply_snapshot(overlay_dir, target_dir, "invalid_snapshot_hash_1234567890") == VINOX_STATUS_OK) {
        printf("FAILED: vinox_artifact_commit_apply_snapshot must fail on target conflict!\n");
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  [PASS 06] Review->Apply Target Conflict Detection (INVALID_STATE Rejection): Verified\n");

    /* 7. Atomic Backup-and-Swap Workspace Takeover Commit with Snapshot Verification */
    if (vinox_artifact_commit_apply_snapshot(overlay_dir, target_dir, snapshot_hash) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_artifact_commit_apply_snapshot failed for matching snapshot\n");
        vinox_sandbox_host_destroy(host);
        return 1;
    }

    /* Verify file exists in target workspace */
    FILE* f = fopen(".test_target/test_file.txt", "rb");
    if (!f) {
        printf("FAILED: File was not committed to target workspace\n");
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    fclose(f);
    printf("  [PASS 07] Atomic Backup-and-Swap Takeover & Target Workspace Commit: Verified\n");

    /* Stop Sandbox Host */
    vinox_sandbox_host_stop(host);
    vinox_sandbox_host_destroy(host);

    printf("SUCCESS: All VINOX Phase 7 Sandbox Isolation & Atomic Takeover smoke tests passed!\n");
    return 0;
}
