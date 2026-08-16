#ifndef VINOX_AGENT_HPP
#define VINOX_AGENT_HPP

#include "vinox_agent.h"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

namespace vinox {
namespace agent {

class ModeController {
public:
    ModeController() {
        m_controller = vinox_mode_controller_create();
        if (!m_controller) throw std::runtime_error("Failed to create ModeController");
    }
    ~ModeController() {
        if (m_controller) vinox_mode_controller_destroy(m_controller);
    }

    ModeController(const ModeController&) = delete;
    ModeController& operator=(const ModeController&) = delete;

    ModeController(ModeController&& other) noexcept : m_controller(other.m_controller) {
        other.m_controller = nullptr;
    }

    vinox_mode get_mode() const {
        return vinox_mode_controller_get_mode(m_controller);
    }

    void set_mode(vinox_mode mode) {
        vinox_status st = vinox_mode_controller_set_mode(m_controller, mode);
        if (st != VINOX_STATUS_OK) throw std::runtime_error("Failed to set mode");
    }

    bool can_execute_mutating_tool() const {
        return vinox_mode_controller_can_execute_mutating_tool(m_controller) != 0;
    }

    vinox_mode_controller* get_raw() const { return m_controller; }

private:
    vinox_mode_controller* m_controller{nullptr};
};

class Plan {
public:
    explicit Plan(const std::string& json_str) {
        m_plan = vinox_plan_create(json_str.c_str());
        if (!m_plan) throw std::runtime_error("Failed to parse plan JSON");
    }
    ~Plan() {
        if (m_plan) vinox_plan_destroy(m_plan);
    }

    Plan(const Plan&) = delete;
    Plan& operator=(const Plan&) = delete;

    bool validate() const {
        return vinox_plan_validate(m_plan) == VINOX_STATUS_OK;
    }

    std::string compute_hash() const {
        char buf[65] = {0};
        if (vinox_plan_compute_hash(m_plan, buf, sizeof(buf)) != VINOX_STATUS_OK) {
            throw std::runtime_error("Failed to compute plan hash");
        }
        return std::string(buf);
    }

    vinox_plan_status get_status() const {
        return vinox_plan_get_status(m_plan);
    }

    void approve(const std::string& expected_hash) {
        if (vinox_plan_approve(m_plan, expected_hash.c_str()) != VINOX_STATUS_OK) {
            throw std::runtime_error("Failed to approve plan: Hash mismatch or invalid state");
        }
    }

    vinox_plan* get_raw() const { return m_plan; }

private:
    vinox_plan* m_plan{nullptr};
};

} // namespace agent
} // namespace vinox

#endif /* VINOX_AGENT_HPP */
