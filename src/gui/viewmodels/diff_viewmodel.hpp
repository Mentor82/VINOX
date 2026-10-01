#pragma once

#include "../backend_interface.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vinox::gui {

class DiffViewModel {
public:
    explicit DiffViewModel(std::shared_ptr<IVinoxBackend> backend);

    void set_backend(std::shared_ptr<IVinoxBackend> backend);

    const DiffArtifact& artifact() const { return artifact_; }
    bool has_pending_diff() const { return !artifact_.hunks.empty(); }
    const std::string& error_message() const { return error_message_; }

    void load_diff(const std::string& run_id);
    void toggle_hunk(uint32_t hunk_id);
    bool apply_selected(const std::string& run_id);
    void clear();

    std::function<void()> on_diff_changed;

private:
    std::shared_ptr<IVinoxBackend> backend_;
    DiffArtifact artifact_;
    std::string error_message_;
};

} // namespace vinox::gui
