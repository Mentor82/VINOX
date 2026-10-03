#include "diff_viewmodel.hpp"

namespace vinox::gui {

DiffViewModel::DiffViewModel(std::shared_ptr<IVinoxBackend> backend)
    : backend_(std::move(backend)) {}

void DiffViewModel::set_backend(std::shared_ptr<IVinoxBackend> backend) {
    backend_ = std::move(backend);
}

void DiffViewModel::load_diff(const std::string& run_id) {
    if (!backend_ || run_id.empty()) return;
    artifact_ = backend_->get_pending_diff(run_id);
    error_message_.clear();
    if (on_diff_changed) on_diff_changed();
}

void DiffViewModel::toggle_hunk(uint32_t hunk_id) {
    for (auto& hunk : artifact_.hunks) {
        if (hunk.id == hunk_id) {
            hunk.is_selected = !hunk.is_selected;
            if (on_diff_changed) on_diff_changed();
            break;
        }
    }
}

bool DiffViewModel::apply_selected(const std::string& run_id) {
    if (!backend_ || artifact_.hunks.empty()) {
        error_message_ = "No pending diff artifact to apply";
        return false;
    }

    std::vector<uint32_t> selected_ids;
    for (const auto& hunk : artifact_.hunks) {
        if (hunk.is_selected) {
            selected_ids.push_back(hunk.id);
        }
    }

    if (selected_ids.empty()) {
        error_message_ = "No hunks selected for application";
        return false;
    }

    // Pass snapshot hash to enforce snapshot-bound mutation
    bool ok = backend_->apply_diff(run_id, artifact_.base_snapshot_hash, selected_ids);
    if (!ok) {
        error_message_ = "Failed to apply diff: snapshot revision mismatch or invalid hunks";
        return false;
    }

    artifact_ = DiffArtifact{};
    error_message_.clear();
    if (on_diff_changed) on_diff_changed();
    return true;
}

void DiffViewModel::clear() {
    artifact_ = DiffArtifact{};
    error_message_.clear();
    if (on_diff_changed) on_diff_changed();
}

} // namespace vinox::gui
