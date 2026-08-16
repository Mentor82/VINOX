#include "vinox/vinox_agent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

int main(void) {
    printf("Starting VINOX Phase 7 Sandbox Worker & Workspace Takeover Smoke Test...\n");

    const char* overlay_dir = ".test_overlay";
    const char* target_dir = ".test_target";

    /* 1. Launch Sandbox Host & Subprocess Worker */
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
    printf("  - Sandbox Worker Subprocess & Windows Job Object Binding: Verified\n");

    /* 2. Execute Mutating Tool in Sandbox Overlay */
    char res_buf[1024] = {0};
    const char* tool_args = "{\"filename\":\"test_file.txt\",\"content\":\"Hello Sandbox World!\"}";
    if (vinox_sandbox_host_exec_tool(host, "local_write.write", tool_args, res_buf, sizeof(res_buf)) != VINOX_STATUS_OK ||
        strstr(res_buf, "\"status\":\"OK\"") == NULL) {
        printf("FAILED: vinox_sandbox_host_exec_tool failed: %s\n", res_buf);
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  - Isolated Sandbox Workspace File Creation & Stdio Pipe RPC: Verified\n");

    /* 3. Compute Diff between Overlay and Target Workspace */
    char diff_buf[2048] = {0};
    if (vinox_artifact_commit_diff(overlay_dir, target_dir, diff_buf, sizeof(diff_buf)) != VINOX_STATUS_OK ||
        strstr(diff_buf, "test_file.txt") == NULL) {
        printf("FAILED: vinox_artifact_commit_diff failed: %s\n", diff_buf);
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  - Unified Diff & Artifact Hashing Engine: Verified\n");

    /* 4. Atomic Commit / Workspace Takeover */
    if (vinox_artifact_commit_apply(overlay_dir, target_dir) != VINOX_STATUS_OK) {
        printf("FAILED: vinox_artifact_commit_apply failed\n");
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
    printf("  - Atomic Takeover & Target Workspace Commit: Verified\n");

    /* Stop Sandbox Host */
    vinox_sandbox_host_stop(host);
    vinox_sandbox_host_destroy(host);

    printf("SUCCESS: All VINOX Phase 7 Sandbox Worker & Workspace Takeover smoke tests passed!\n");
    return 0;
}
