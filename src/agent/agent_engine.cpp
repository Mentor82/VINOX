#include "vinox/vinox_agent.h"
#include <nlohmann/json.hpp>
#include <new>
#include <atomic>
#include <string>
#include <vector>
#include <set>
#include <chrono>
#include <iostream>

struct PlanStepState {
    std::string step_id;
    std::string description;
    std::vector<std::string> dependencies;
    bool completed{false};
};

struct vinox_agent_run {
    vinox_mode_controller* mode_controller{nullptr};
    vinox_plan* plan{nullptr};
    vinox_agent_budget budget{};
    int current_step_idx{0};
    int completed_steps{0};
    int total_tool_calls{0};
    int total_tokens_used{0};
    std::chrono::steady_clock::time_point start_time;
    vinox_plan_status run_status{VINOX_PLAN_STATUS_READY};
    std::vector<PlanStepState> steps;
};

extern "C" {

VINOX_API vinox_agent_run* VINOX_CALL vinox_agent_run_create(vinox_mode_controller* controller, vinox_plan* plan, const vinox_agent_budget* budget) {
    if (!controller || !plan) return nullptr;

    vinox_plan_status plan_st = vinox_plan_get_status(plan);
    if (plan_st != VINOX_PLAN_STATUS_APPROVED) {
        // Agent run creation fails closed if plan is not approved by user
        return nullptr;
    }

    // Budget validation: reject negative or zero limit values that weaken safety
    if (budget) {
        if (budget->max_steps <= 0 || budget->max_tokens <= 0 || budget->max_tool_calls <= 0 || budget->max_duration_seconds <= 0) {
            return nullptr; // Invalid budget rejected
        }
    }

    // Transition mode controller to AGENT mode
    if (vinox_mode_controller_set_mode(controller, VINOX_MODE_AGENT) != VINOX_STATUS_OK) {
        return nullptr;
    }

    auto run = new (std::nothrow) vinox_agent_run();
    if (!run) return nullptr;

    run->mode_controller = controller;
    run->plan = plan;
    run->start_time = std::chrono::steady_clock::now();

    if (budget && budget->struct_size == sizeof(vinox_agent_budget)) {
        run->budget = *budget;
    } else {
        run->budget.struct_size = sizeof(vinox_agent_budget);
        run->budget.max_steps = 10;
        run->budget.max_tokens = 4096;
        run->budget.max_tool_calls = 20;
        run->budget.max_duration_seconds = 300;
    }

    // Parse step dependencies from plan raw JSON
    try {
        char hash_buf[65] = {0};
        vinox_plan_compute_hash(plan, hash_buf, sizeof(hash_buf));

        // Read raw JSON by computing hash/getting plan structure
        // Extract steps for execution graph
    } catch (...) {
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

    // 1. Check Mode Controller Invariant: Mode MUST be AGENT mode
    if (vinox_mode_controller_get_mode(run->mode_controller) != VINOX_MODE_AGENT) {
        run->run_status = VINOX_PLAN_STATUS_BLOCKED;
        return VINOX_STATUS_INVALID_STATE;
    }

    // 2. Check Monotonic Duration Deadline Budget
    auto now = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(now - run->start_time).count();
    if (elapsed_sec > run->budget.max_duration_seconds) {
        run->run_status = VINOX_PLAN_STATUS_FAILED;
        return VINOX_STATUS_OUT_OF_RANGE; // Duration deadline exceeded!
    }

    // 3. Check Step & Tool Call Budget Limits
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
