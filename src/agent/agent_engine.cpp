#include "vinox/vinox_agent.h"
#include "vinox/tools.h"
#include <nlohmann/json.hpp>
#include <new>
#include <atomic>
#include <string>
#include <vector>
#include <set>
#include <chrono>
#include <cstring>
#include <iostream>

struct AgentStepToolCall {
    std::string name;
    std::string arguments_json;
};

struct AgentPlanStep {
    std::string step_id;
    std::string description;
    std::vector<std::string> dependencies;
    std::vector<AgentStepToolCall> tool_calls;
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
    std::vector<AgentPlanStep> steps;
    vinox_tool_registry* registry{nullptr};
    vinox_policy_engine* policy_engine{nullptr};
    vinox_sandbox_host* sandbox_host{nullptr};
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

    // Nephy Finding 3: Fail-Closed Plan Extraction on Run Creation
    char plan_json_buf[65536] = {0};
    if (vinox_plan_get_json(plan, plan_json_buf, sizeof(plan_json_buf)) != VINOX_STATUS_OK) {
        delete run;
        return nullptr; // Plan JSON extraction failed!
    }

    try {
        auto j = nlohmann::json::parse(plan_json_buf);
        if (!j.contains("steps") || !j["steps"].is_array() || j["steps"].empty()) {
            delete run;
            return nullptr; // Empty or invalid steps array rejected fail-closed!
        }

        for (const auto& sj : j["steps"]) {
            AgentPlanStep step;
            step.step_id = sj.value("step_id", "");
            step.description = sj.value("description", "");
            step.completed = false;

            if (sj.contains("dependencies") && sj["dependencies"].is_array()) {
                for (const auto& dep : sj["dependencies"]) {
                    if (dep.is_string()) step.dependencies.push_back(dep.get<std::string>());
                }
            }

            if (sj.contains("tool_calls") && sj["tool_calls"].is_array()) {
                for (const auto& tc : sj["tool_calls"]) {
                    AgentStepToolCall tool_call;
                    tool_call.name = tc.value("name", "");
                    if (tc.contains("arguments") && tc["arguments"].is_object()) {
                        tool_call.arguments_json = tc["arguments"].dump();
                    } else {
                        tool_call.arguments_json = "{}";
                    }
                    step.tool_calls.push_back(tool_call);
                }
            }
            run->steps.push_back(step);
        }
    } catch (...) {
        delete run;
        return nullptr; // Malformed plan JSON rejected fail-closed!
    }

    if (run->steps.empty()) {
        delete run;
        return nullptr;
    }

    run->run_status = VINOX_PLAN_STATUS_RUNNING;
    return run;
}

VINOX_API void VINOX_CALL vinox_agent_run_destroy(vinox_agent_run* run) {
    if (run) delete run;
}

VINOX_API vinox_status VINOX_CALL vinox_agent_run_set_governance(vinox_agent_run* run, vinox_tool_registry* registry, vinox_policy_engine* policy_engine) {
    if (!run) return VINOX_STATUS_INVALID_ARGUMENT;
    run->registry = registry;
    run->policy_engine = policy_engine;
    return VINOX_STATUS_OK;
}

VINOX_API vinox_status VINOX_CALL vinox_agent_run_set_sandbox(vinox_agent_run* run, vinox_sandbox_host* sandbox_host) {
    if (!run) return VINOX_STATUS_INVALID_ARGUMENT;
    run->sandbox_host = sandbox_host;
    return VINOX_STATUS_OK;
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

    // 3. Find next unexecuted step whose dependencies are ALL completed
    AgentPlanStep* ready_step = nullptr;
    bool all_completed = true;
    bool blocked_by_dependencies = false;

    for (auto& s : run->steps) {
        if (!s.completed) {
            all_completed = false;

            // Check if all dependencies are completed
            bool deps_ok = true;
            for (const auto& dep_id : s.dependencies) {
                bool dep_found_and_completed = false;
                for (const auto& other : run->steps) {
                    if (other.step_id == dep_id && other.completed) {
                        dep_found_and_completed = true;
                        break;
                    }
                }
                if (!dep_found_and_completed) {
                    deps_ok = false;
                    break;
                }
            }

            if (deps_ok) {
                ready_step = &s;
                break;
            } else {
                blocked_by_dependencies = true;
            }
        }
    }

    if (all_completed || run->steps.empty()) {
        run->run_status = VINOX_PLAN_STATUS_COMPLETED;
        return VINOX_STATUS_OK;
    }

    if (!ready_step) {
        if (blocked_by_dependencies) {
            run->run_status = VINOX_PLAN_STATUS_BLOCKED;
            return VINOX_STATUS_INVALID_STATE; // Blocked by unsatisfied dependencies!
        }
        run->run_status = VINOX_PLAN_STATUS_COMPLETED;
        return VINOX_STATUS_OK;
    }

    // 4. Check Step Budget Limits
    if (run->completed_steps >= run->budget.max_steps) {
        run->run_status = VINOX_PLAN_STATUS_FAILED;
        return VINOX_STATUS_OUT_OF_RANGE;
    }

    // 5. Mandatory Governance & Mandatory Sandbox Executor Gates
    size_t step_token_budget_units = (ready_step->description.length() / 4) + 16;

    if (!ready_step->tool_calls.empty()) {
        // Nephy Finding 1: Mandatory Governance Gate (Fail-Closed if missing!)
        if (!run->registry || !run->policy_engine) {
            run->run_status = VINOX_PLAN_STATUS_FAILED;
            return VINOX_STATUS_PERMISSION_DENIED;
        }

        // Nephy Follow-up Review Fix: Mandatory Sandbox Executor Gate (Fail-Closed if missing!)
        if (!run->sandbox_host) {
            run->run_status = VINOX_PLAN_STATUS_FAILED;
            return VINOX_STATUS_INVALID_STATE; // Tool step execution impossible without bound sandbox executor!
        }
    }

    for (const auto& tc : ready_step->tool_calls) {
        if (run->total_tool_calls >= run->budget.max_tool_calls) {
            run->run_status = VINOX_PLAN_STATUS_FAILED;
            return VINOX_STATUS_OUT_OF_RANGE;
        }

        // Phase 6 Tool Registry Lookup & Parameter Schema Validation
        char tpool[4096] = {0};
        vinox_tool_definition tdef;
        std::memset(&tdef, 0, sizeof(tdef));
        tdef.struct_size = sizeof(tdef);

        if (vinox_tool_registry_find_tool(run->registry, tc.name.c_str(), &tdef, tpool, sizeof(tpool)) != VINOX_STATUS_OK) {
            run->run_status = VINOX_PLAN_STATUS_FAILED;
            return VINOX_STATUS_NOT_FOUND; // Tool not registered!
        }

        char val_err[512] = {0};
        if (vinox_tool_registry_validate_arguments(run->registry, tc.name.c_str(), tc.arguments_json.c_str(), val_err, sizeof(val_err)) != VINOX_STATUS_OK) {
            run->run_status = VINOX_PLAN_STATUS_FAILED;
            return VINOX_STATUS_INVALID_ARGUMENT; // Schema validation error!
        }

        // Phase 6 Policy Engine Authorization Check
        vinox_tool_call_request req_call;
        std::memset(&req_call, 0, sizeof(req_call));
        req_call.struct_size = sizeof(req_call);
        req_call.call_id = "agent_run_call";
        req_call.tool_name = tc.name.c_str();
        req_call.arguments_json = tc.arguments_json.c_str();

        vinox_policy_decision pdecision;
        std::memset(&pdecision, 0, sizeof(pdecision));
        pdecision.struct_size = sizeof(pdecision);
        char reason[256] = {0};

        if (vinox_policy_engine_evaluate(run->policy_engine, &req_call, &tdef, &pdecision, reason, sizeof(reason)) != VINOX_STATUS_OK || !pdecision.allowed) {
            run->run_status = VINOX_PLAN_STATUS_BLOCKED;
            return VINOX_STATUS_PERMISSION_DENIED; // Governance Policy Refusal Gate!
        }

        // ACTUAL TOOL EXECUTION DISPATCH via Sandbox Worker!
        char exec_res_buf[4096] = {0};
        vinox_status exec_st = vinox_sandbox_host_exec_tool(run->sandbox_host, tc.name.c_str(), tc.arguments_json.c_str(), exec_res_buf, sizeof(exec_res_buf));
        if (exec_st != VINOX_STATUS_OK || strstr(exec_res_buf, "\"status\":\"OK\"") == NULL) {
            run->run_status = VINOX_PLAN_STATUS_FAILED;
            return VINOX_STATUS_RUNTIME_ERROR; // Real tool execution failure!
        }

        run->total_tool_calls++;
        step_token_budget_units += (tc.arguments_json.length() / 4) + 16;
    }

    // Token & Cost Accounting
    run->total_tokens_used += static_cast<int>(step_token_budget_units);
    if (run->total_tokens_used >= run->budget.max_tokens) {
        run->run_status = VINOX_PLAN_STATUS_FAILED;
        return VINOX_STATUS_OUT_OF_RANGE; // Max tokens budget exceeded!
    }

    // Mark Step Completed ONLY after Governance & ACTUAL Sandbox Execution succeed 100%!
    ready_step->completed = true;
    run->completed_steps++;
    run->current_step_idx++;

    // Check if all steps in plan completed
    bool all_done = true;
    for (const auto& s : run->steps) {
        if (!s.completed) {
            all_done = false;
            break;
        }
    }

    if (all_done) {
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

VINOX_API vinox_plan_status VINOX_CALL vinox_agent_run_get_status(const vinox_agent_run* run) {
    if (!run) return VINOX_PLAN_STATUS_FAILED;
    return run->run_status;
}

VINOX_API int VINOX_CALL vinox_agent_run_get_completed_steps(const vinox_agent_run* run) {
    if (!run) return 0;
    return run->completed_steps;
}

} // extern "C"
