#include "vinox/vinox_agent.h"
#include <new>
#include <atomic>
#include <string>
#include <vector>

struct vinox_agent_run {
    vinox_mode_controller* mode_controller{nullptr};
    vinox_plan* plan{nullptr};
    vinox_agent_budget budget{};
    int current_step_idx{0};
    int completed_steps{0};
    int total_tool_calls{0};
    vinox_plan_status run_status{VINOX_PLAN_STATUS_READY};
};

extern "C" {

VINOX_API vinox_agent_run* VINOX_CALL vinox_agent_run_create(vinox_mode_controller* controller, vinox_plan* plan, const vinox_agent_budget* budget) {
    if (!controller || !plan) return nullptr;

    vinox_plan_status plan_st = vinox_plan_get_status(plan);
    if (plan_st != VINOX_PLAN_STATUS_APPROVED) {
        // Agent run creation fails closed if plan is not approved by user
        return nullptr;
    }

    // Mode must be AGENT to run agent steps
    vinox_mode_controller_set_mode(controller, VINOX_MODE_AGENT);

    auto run = new (std::nothrow) vinox_agent_run();
    if (!run) return nullptr;

    run->mode_controller = controller;
    run->plan = plan;
    if (budget && budget->struct_size == sizeof(vinox_agent_budget)) {
        run->budget = *budget;
    } else {
        run->budget.struct_size = sizeof(vinox_agent_budget);
        run->budget.max_steps = 10;
        run->budget.max_tokens = 4096;
        run->budget.max_tool_calls = 20;
        run->budget.max_duration_seconds = 300;
    }

    run->run_status = VINOX_PLAN_STATUS_RUNNING;
    return run;
}

VINOX_API void VINOX_CALL vinox_agent_run_destroy(vinox_agent_run* run) {
    if (run) delete run;
}

VINOX_API vinox_status VINOX_CALL vinox_agent_run_step(vinox_agent_run* run) {
    if (!run) return VINOX_STATUS_INVALID_ARGUMENT;
    if (run->run_status != VINOX_PLAN_STATUS_RUNNING) return VINOX_STATUS_INVALID_STATE;

    // Check mode invariant
    if (vinox_mode_controller_get_mode(run->mode_controller) != VINOX_MODE_AGENT) {
        run->run_status = VINOX_PLAN_STATUS_BLOCKED;
        return VINOX_STATUS_INVALID_STATE;
    }

    // Check budget limits
    if (run->completed_steps >= run->budget.max_steps || run->total_tool_calls >= run->budget.max_tool_calls) {
        run->run_status = VINOX_PLAN_STATUS_FAILED;
        return VINOX_STATUS_OUT_OF_RANGE;
    }

    // Execute step logic
    run->completed_steps++;
    run->total_tool_calls++;
    run->current_step_idx++;

    if (run->completed_steps >= run->budget.max_steps) {
        run->run_status = VINOX_PLAN_STATUS_COMPLETED;
    }

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status VINOX_CALL vinox_agent_run_pause(vinox_agent_run* run) {
    if (!run) return VINOX_STATUS_INVALID_ARGUMENT;
    if (run->run_status != VINOX_PLAN_STATUS_RUNNING) return VINOX_STATUS_INVALID_STATE;
    run->run_status = VINOX_PLAN_STATUS_PAUSED;
    return VINOX_STATUS_OK;
}

VINOX_API vinox_status VINOX_CALL vinox_agent_run_resume(vinox_agent_run* run) {
    if (!run) return VINOX_STATUS_INVALID_ARGUMENT;
    if (run->run_status != VINOX_PLAN_STATUS_PAUSED) return VINOX_STATUS_INVALID_STATE;
    run->run_status = VINOX_PLAN_STATUS_RUNNING;
    return VINOX_STATUS_OK;
}

VINOX_API vinox_status VINOX_CALL vinox_agent_run_cancel(vinox_agent_run* run) {
    if (!run) return VINOX_STATUS_INVALID_ARGUMENT;
    run->run_status = VINOX_PLAN_STATUS_CANCELLED;
    return VINOX_STATUS_OK;
}

VINOX_API int VINOX_CALL vinox_agent_run_get_completed_steps(const vinox_agent_run* run) {
    if (!run) return 0;
    return run->completed_steps;
}

} // extern "C"
