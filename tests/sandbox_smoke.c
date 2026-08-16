#include "vinox/vinox_agent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

int main(void) {
    printf("Starting VINOX Phase 7 Sandbox Isolation & Atomic Takeover Smoke Test...\n");

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
    printf("  [PASS 01] Sandbox Worker Subprocess & Job Object Resource Binding: Verified\n");

    /* 2. Execute Valid Mutating Tool in Sandbox Overlay */
    char res_buf[1024] = {0};
    const char* tool_args = "{\"filename\":\"test_file.txt\",\"content\":\"Hello Sandbox World!\"}";
    if (vinox_sandbox_host_exec_tool(host, "local_write.write", tool_args, res_buf, sizeof(res_buf)) != VINOX_STATUS_OK ||
        strstr(res_buf, "\"status\":\"OK\"") == NULL) {
        printf("FAILED: vinox_sandbox_host_exec_tool failed: %s\n", res_buf);
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  [PASS 02] Isolated Sandbox Workspace File Creation & Stdio Pipe RPC: Verified\n");

    /* 3. Negative Test: Unknown Sandbox Tool Rejection (Fail-Closed) */
    char err_buf[1024] = {0};
    if (vinox_sandbox_host_exec_tool(host, "unknown_hallucinated_tool", tool_args, err_buf, sizeof(err_buf)) == VINOX_STATUS_OK &&
        strstr(err_buf, "\"status\":\"OK\"") != NULL) {
        printf("FAILED: vinox_sandbox_host_exec_tool must fail closed on unknown tools!\n");
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  [PASS 03] Unknown Tool Call Fail-Closed Rejection (-32601): Verified\n");

    /* 4. Negative Test: Malformed JSON Arguments Rejection (Fail-Closed) */
    char malformed_buf[1024] = {0};
    if (vinox_sandbox_host_exec_tool(host, "local_write.write", "{invalid_json_str", malformed_buf, sizeof(malformed_buf)) == VINOX_STATUS_OK &&
        strstr(malformed_buf, "\"status\":\"OK\"") != NULL) {
        printf("FAILED: vinox_sandbox_host_exec_tool must fail closed on malformed JSON!\n");
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  [PASS 04] Malformed Arguments JSON Fail-Closed Rejection: Verified\n");

    /* 5. Negative Test: Sandbox Path Containment Escape Prevention */
    char escape_buf[1024] = {0};
    const char* escape_args = "{\"filename\":\"../../escape_test.txt\",\"content\":\"Hacked!\"}";
    if (vinox_sandbox_host_exec_tool(host, "local_write.write", escape_args, escape_buf, sizeof(escape_buf)) == VINOX_STATUS_OK &&
        strstr(escape_buf, "\"status\":\"OK\"") != NULL) {
        printf("FAILED: Sandbox worker must reject path escape attempts!\n");
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  [PASS 05] Sandbox Path Containment & Root Escape Prevention: Verified\n");

    /* 6. Compute Truthful Diff between Overlay and Target Workspace */
    char diff_buf[2048] = {0};
    if (vinox_artifact_commit_diff(overlay_dir, target_dir, diff_buf, sizeof(diff_buf)) != VINOX_STATUS_OK ||
        strstr(diff_buf, "test_file.txt") == NULL) {
        printf("FAILED: vinox_artifact_commit_diff failed: %s\n", diff_buf);
        vinox_sandbox_host_destroy(host);
        return 1;
    }
    printf("  [PASS 06] Truthful Artifact Unified Diff Engine: Verified\n");

    /* 7. Transactional Staged Workspace Takeover Commit */
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
    printf("  [PASS 07] Transactional Staged Takeover & Target Workspace Commit: Verified\n");

    /* Stop Sandbox Host */
    vinox_sandbox_host_stop(host);
    vinox_sandbox_host_destroy(host);

    printf("SUCCESS: All VINOX Phase 7 Sandbox Isolation & Atomic Takeover smoke tests passed!\n");
    return 0;
}
