#include "plan_viewmodel.hpp"

namespace vinox::gui {

PlanViewModel::PlanViewModel(std::shared_ptr<IVinoxBackend> backend)
    : backend_(std::move(backend)) {}

void PlanViewModel::set_backend(std::shared_ptr<IVinoxBackend> backend) {
    backend_ = std::move(backend);
}

bool PlanViewModel::generate_plan(const std::string& task) {
    if (!backend_ || task.empty()) return false;
    error_message_.clear();

    current_plan_ = backend_->create_plan(task);
    if (current_plan_.plan_id.empty()) {
        error_message_ = "Failed to generate plan";
        return false;
    }

    if (on_plan_changed) on_plan_changed();
    return true;
}

bool PlanViewModel::approve_plan(const std::string& expected_hash) {
    if (!backend_ || current_plan_.plan_id.empty()) {
        error_message_ = "No active plan to approve";
        return false;
    }

    // Fail-Closed Stale-State Protection: UI checks hash equality before submission
    if (expected_hash != current_plan_.plan_hash) {
        error_message_ = "Plan approval hash mismatch: stale plan state detected!";
        return false;
    }

    bool ok = backend_->approve_plan(current_plan_.plan_id, expected_hash);
    if (!ok) {
        error_message_ = "Backend rejected plan approval";
        return false;
    }

    current_plan_.is_approved = true;
    error_message_.clear();
    if (on_plan_changed) on_plan_changed();
    return true;
}

void PlanViewModel::clear() {
    current_plan_ = PlanData{};
    error_message_.clear();
    if (on_plan_changed) on_plan_changed();
}

} // namespace vinox::gui
