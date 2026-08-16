#ifndef VINOX_TOOLS_HPP
#define VINOX_TOOLS_HPP

#include <memory>
#include <string>
#include <vector>
#include "vinox/tools.h"

namespace vinox {
namespace tools {

class ToolRegistry {
public:
    ToolRegistry() : registry_(nullptr) {
        vinox_tool_registry_create(&registry_);
    }

    ~ToolRegistry() {
        if (registry_) {
            vinox_tool_registry_destroy(registry_);
            registry_ = nullptr;
        }
    }

    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

    ToolRegistry(ToolRegistry&& other) noexcept : registry_(other.registry_) {
        other.registry_ = nullptr;
    }

    ToolRegistry& operator=(ToolRegistry&& other) noexcept {
        if (this != &other) {
            if (registry_) vinox_tool_registry_destroy(registry_);
            registry_ = other.registry_;
            other.registry_ = nullptr;
        }
        return *this;
    }

    vinox_tool_registry* get() const { return registry_; }

    vinox_status register_tool(const std::string& name, const std::string& description, const std::string& json_schema, vinox_security_class sec_class) {
        vinox_tool_definition def{};
        def.struct_size = sizeof(def);
        def.name = name.c_str();
        def.description = description.c_str();
        def.parameters_json_schema = json_schema.c_str();
        def.security_class = static_cast<uint32_t>(sec_class);
        return vinox_tool_registry_register_tool(registry_, &def);
    }

    vinox_status validate_arguments(const std::string& tool_name, const std::string& args_json, std::string& err_out) const {
        char err_buf[512];
        err_buf[0] = '\0';
        vinox_status status = vinox_tool_registry_validate_arguments(registry_, tool_name.c_str(), args_json.c_str(), err_buf, sizeof(err_buf));
        if (status != VINOX_STATUS_OK) {
            err_out = err_buf;
        }
        return status;
    }

    std::string format_openai_schema() const {
        size_t req_sz = 0;
        vinox_tools_format_openai_schema(registry_, nullptr, 0, &req_sz);
        if (req_sz == 0) return "[]";
        std::vector<char> buf(req_sz);
        if (vinox_tools_format_openai_schema(registry_, buf.data(), buf.size(), nullptr) == VINOX_STATUS_OK) {
            return std::string(buf.data());
        }
        return "[]";
    }

private:
    vinox_tool_registry* registry_;
};

class PolicyEngine {
public:
    PolicyEngine() : engine_(nullptr) {
        vinox_policy_engine_create(&engine_);
    }

    ~PolicyEngine() {
        if (engine_) {
            vinox_policy_engine_destroy(engine_);
            engine_ = nullptr;
        }
    }

    PolicyEngine(const PolicyEngine&) = delete;
    PolicyEngine& operator=(const PolicyEngine&) = delete;

    vinox_policy_engine* get() const { return engine_; }

    vinox_status set_rule(const std::string& pattern, vinox_security_class max_class, vinox_approval_mode approval) {
        return vinox_policy_engine_set_rule(engine_, pattern.c_str(), static_cast<uint32_t>(max_class), static_cast<uint32_t>(approval));
    }

private:
    vinox_policy_engine* engine_;
};

} // namespace tools
} // namespace vinox

#endif // VINOX_TOOLS_HPP
