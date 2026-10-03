#pragma once

#include "../backend_interface.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vinox::gui {

class AgentViewModel {
public:
    explicit AgentViewModel(std::shared_ptr<IVinoxBackend> backend);

    void set_backend(std::shared_ptr<IVinoxBackend> backend);

    const std::vector<AgentEvent>& events() const { return events_; }
    const AgentRunStatus& status() const { return status_; }
    bool is_running() const { return status_.state == "running"; }
    const std::string& error_message() const { return error_message_; }

    bool start_run(const std::string& plan_id, const std::string& plan_hash, const std::string& workspace_dir);
    bool cancel_run();
    void refresh_status();
    void clear();

    std::function<void()> on_events_updated;
    std::function<void()> on_status_changed;

private:
    std::shared_ptr<IVinoxBackend> backend_;
    std::vector<AgentEvent> events_;
    AgentRunStatus status_{"", "idle", 0, 0, 0, 100000, ""};
    std::string error_message_;
};

} // namespace vinox::gui
