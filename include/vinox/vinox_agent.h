#ifndef VINOX_AGENT_H
#define VINOX_AGENT_H

#include "vinox.h"
#include "vinox/tools.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vinox_mode {
    VINOX_MODE_CHAT = 0,
    VINOX_MODE_PLAN = 1,
    VINOX_MODE_AGENT = 2
} vinox_mode;

typedef enum vinox_plan_status {
    VINOX_PLAN_STATUS_DRAFT = 0,
    VINOX_PLAN_STATUS_READY = 1,
    VINOX_PLAN_STATUS_APPROVED = 2,
    VINOX_PLAN_STATUS_RUNNING = 3,
    VINOX_PLAN_STATUS_PAUSED = 4,
    VINOX_PLAN_STATUS_BLOCKED = 5,
    VINOX_PLAN_STATUS_COMPLETED = 6,
    VINOX_PLAN_STATUS_FAILED = 7,
    VINOX_PLAN_STATUS_CANCELLED = 8
} vinox_plan_status;

typedef struct vinox_agent_budget {
    uint32_t struct_size;
    int max_steps;
    int max_tokens;
    int max_tool_calls;
    int max_duration_seconds;
} vinox_agent_budget;

typedef struct vinox_mode_controller vinox_mode_controller;
typedef struct vinox_plan vinox_plan;
typedef struct vinox_agent_run vinox_agent_run;

/* Mode Controller */
VINOX_API vinox_mode_controller* VINOX_CALL vinox_mode_controller_create(void);
VINOX_API void VINOX_CALL vinox_mode_controller_destroy(vinox_mode_controller* controller);
VINOX_API vinox_mode VINOX_CALL vinox_mode_controller_get_mode(const vinox_mode_controller* controller);
VINOX_API vinox_status VINOX_CALL vinox_mode_controller_set_mode(vinox_mode_controller* controller, vinox_mode new_mode);
VINOX_API int VINOX_CALL vinox_mode_controller_can_execute_mutating_tool(const vinox_mode_controller* controller);

/* Plan Model */
VINOX_API vinox_plan* VINOX_CALL vinox_plan_create(const char* json_str);
VINOX_API void VINOX_CALL vinox_plan_destroy(vinox_plan* plan);
VINOX_API vinox_status VINOX_CALL vinox_plan_validate(const vinox_plan* plan);
VINOX_API vinox_status VINOX_CALL vinox_plan_compute_hash(const vinox_plan* plan, char* hash_buf, size_t hash_buf_sz);
VINOX_API vinox_plan_status VINOX_CALL vinox_plan_get_status(const vinox_plan* plan);
VINOX_API vinox_status VINOX_CALL vinox_plan_approve(vinox_plan* plan, const char* expected_hash);
VINOX_API vinox_status VINOX_CALL vinox_plan_get_json(const vinox_plan* plan, char* out_buf, size_t out_buf_sz);

/* Sandbox Host & Worker IPC */
typedef struct vinox_sandbox_host vinox_sandbox_host;

/* Agent Engine & Run */
VINOX_API vinox_agent_run* VINOX_CALL vinox_agent_run_create(vinox_mode_controller* controller, vinox_plan* plan, const vinox_agent_budget* budget);
VINOX_API void VINOX_CALL vinox_agent_run_destroy(vinox_agent_run* run);
VINOX_API vinox_status VINOX_CALL vinox_agent_run_set_governance(vinox_agent_run* run, vinox_tool_registry* registry, vinox_policy_engine* policy_engine);
VINOX_API vinox_status VINOX_CALL vinox_agent_run_set_sandbox(vinox_agent_run* run, vinox_sandbox_host* sandbox_host);
VINOX_API vinox_status VINOX_CALL vinox_agent_run_step(vinox_agent_run* run);
VINOX_API vinox_status VINOX_CALL vinox_agent_run_pause(vinox_agent_run* run);
VINOX_API vinox_status VINOX_CALL vinox_agent_run_resume(vinox_agent_run* run);
VINOX_API vinox_status VINOX_CALL vinox_agent_run_cancel(vinox_agent_run* run);
VINOX_API vinox_plan_status VINOX_CALL vinox_agent_run_get_status(const vinox_agent_run* run);
VINOX_API int VINOX_CALL vinox_agent_run_get_completed_steps(const vinox_agent_run* run);

VINOX_API vinox_sandbox_host* VINOX_CALL vinox_sandbox_host_create(const char* overlay_dir);
VINOX_API void VINOX_CALL vinox_sandbox_host_destroy(vinox_sandbox_host* host);
VINOX_API vinox_status VINOX_CALL vinox_sandbox_host_start(vinox_sandbox_host* host, const char* worker_exe_path);
VINOX_API vinox_status VINOX_CALL vinox_sandbox_host_exec_tool(vinox_sandbox_host* host, const char* tool_name, const char* args_json, char* out_buf, size_t out_buf_sz);
VINOX_API vinox_status VINOX_CALL vinox_sandbox_host_stop(vinox_sandbox_host* host);

/* Artifact Takeover Commit */
VINOX_API vinox_status VINOX_CALL vinox_artifact_commit_diff(const char* overlay_dir, const char* target_dir, char* diff_buf, size_t diff_buf_sz);
VINOX_API vinox_status VINOX_CALL vinox_artifact_commit_apply(const char* overlay_dir, const char* target_dir);
VINOX_API vinox_status VINOX_CALL vinox_artifact_commit_apply_snapshot(const char* overlay_dir, const char* target_dir, const char* expected_snapshot_hash);

#ifdef __cplusplus
}
#endif

#endif /* VINOX_AGENT_H */
