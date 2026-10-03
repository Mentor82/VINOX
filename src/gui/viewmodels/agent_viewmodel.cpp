#include "agent_viewmodel.hpp"

namespace vinox::gui {

AgentViewModel::AgentViewModel(std::shared_ptr<IVinoxBackend> backend)
    : backend_(std::move(backend)) {}

void AgentViewModel::set_backend(std::shared_ptr<IVinoxBackend> backend) {
    backend_ = std::move(backend);
}

bool AgentViewModel::start_run(const std::string& plan_id, const std::string& plan_hash, const std::string& workspace_dir) {
    if (!backend_ || plan_id.empty() || plan_hash.empty()) {
        error_message_ = "Missing required plan credentials to start agent run";
        return false;
    }
    error_message_.clear();
    events_.clear();

    auto event_cb = [this](const AgentEvent& ev) {
        events_.push_back(ev);
        if (on_events_updated) on_events_updated();
    };

    std::string run_id = backend_->start_agent_run(plan_id, plan_hash, workspace_dir, event_cb);
    if (run_id.empty()) {
        error_message_ = "Backend rejected agent run startup (unapproved plan or hash mismatch)";
        status_.state = "failed";
        if (on_status_changed) on_status_changed();
        return false;
    }

    status_ = backend_->get_agent_run_status(run_id);
    if (on_status_changed) on_status_changed();
    return true;
}

bool AgentViewModel::cancel_run() {
    if (!backend_ || status_.run_id.empty()) return false;
    bool ok = backend_->cancel_agent_run(status_.run_id);
    if (ok) {
        status_.state = "cancelled";
        if (on_status_changed) on_status_changed();
    }
    return ok;
}

void AgentViewModel::refresh_status() {
    if (!backend_ || status_.run_id.empty()) return;
    status_ = backend_->get_agent_run_status(status_.run_id);
    if (on_status_changed) on_status_changed();
}

void AgentViewModel::clear() {
    events_.clear();
    status_ = {"", "idle", 0, 0, 0, 100000, ""};
    error_message_.clear();
    if (on_events_updated) on_events_updated();
    if (on_status_changed) on_status_changed();
}

} // namespace vinox::gui
