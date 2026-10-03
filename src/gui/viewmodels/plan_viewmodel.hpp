#pragma once

#include "../backend_interface.hpp"

#include <functional>
#include <memory>
#include <string>

namespace vinox::gui {

class PlanViewModel {
public:
    explicit PlanViewModel(std::shared_ptr<IVinoxBackend> backend);

    void set_backend(std::shared_ptr<IVinoxBackend> backend);

    const PlanData& current_plan() const { return current_plan_; }
    bool has_active_plan() const { return !current_plan_.plan_id.empty(); }
    bool is_approved() const { return current_plan_.is_approved; }
    const std::string& error_message() const { return error_message_; }

    bool generate_plan(const std::string& task);
    bool approve_plan(const std::string& expected_hash);
    void clear();

    std::function<void()> on_plan_changed;

private:
    std::shared_ptr<IVinoxBackend> backend_;
    PlanData current_plan_;
    std::string error_message_;
};

} // namespace vinox::gui
