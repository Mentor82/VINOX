#ifndef VINOX_PLUGINS_HPP
#define VINOX_PLUGINS_HPP

#include <memory>
#include <string>
#include <vector>
#include "vinox/plugins.h"
#include "vinox/tools.hpp"

namespace vinox {
namespace plugins {

class ToolPlugin {
public:
    ToolPlugin() : plugin_(nullptr) {}
    explicit ToolPlugin(vinox_tool_plugin* plugin) : plugin_(plugin) {}

    ~ToolPlugin() {
        reset();
    }

    ToolPlugin(const ToolPlugin&) = delete;
    ToolPlugin& operator=(const ToolPlugin&) = delete;

    ToolPlugin(ToolPlugin&& other) noexcept : plugin_(other.plugin_) {
        other.plugin_ = nullptr;
    }

    ToolPlugin& operator=(ToolPlugin&& other) noexcept {
        if (this != &other) {
            reset();
            plugin_ = other.plugin_;
            other.plugin_ = nullptr;
        }
        return *this;
    }

    void reset() {
        if (plugin_) {
            vinox_plugin_destroy(plugin_);
            plugin_ = nullptr;
        }
    }

    vinox_tool_plugin* get() const { return plugin_; }
    bool is_valid() const { return plugin_ != nullptr; }

    std::string name() const {
        return (plugin_ && plugin_->name) ? plugin_->name : "";
    }

    std::string version() const {
        return (plugin_ && plugin_->version) ? plugin_->version : "";
    }

    std::string description() const {
        return (plugin_ && plugin_->description) ? plugin_->description : "";
    }

    uint32_t scopes() const {
        return plugin_ ? plugin_->declared_scopes : 0;
    }

    vinox_status register_with(vinox::tools::ToolRegistry& registry) {
        if (!plugin_) return VINOX_STATUS_INVALID_ARGUMENT;
        return vinox_plugin_register(registry.get(), plugin_);
    }

    vinox_status register_with(vinox_tool_registry* registry) {
        if (!plugin_) return VINOX_STATUS_INVALID_ARGUMENT;
        return vinox_plugin_register(registry, plugin_);
    }

    // Factory methods for standard plugins
    static ToolPlugin create_std_fs(const std::string& workspace_root) {
        vinox_tool_plugin* p = nullptr;
        if (vinox_plugin_std_fs_create(workspace_root.c_str(), &p) == VINOX_STATUS_OK) {
            return ToolPlugin(p);
        }
        return ToolPlugin();
    }

    static ToolPlugin create_std_math() {
        vinox_tool_plugin* p = nullptr;
        if (vinox_plugin_std_math_create(&p) == VINOX_STATUS_OK) {
            return ToolPlugin(p);
        }
        return ToolPlugin();
    }

    static ToolPlugin create_std_time() {
        vinox_tool_plugin* p = nullptr;
        if (vinox_plugin_std_time_create(&p) == VINOX_STATUS_OK) {
            return ToolPlugin(p);
        }
        return ToolPlugin();
    }

    static ToolPlugin create_std_retrieval(vinox_storage_engine* storage, vinox_embedding_engine* embedding) {
        vinox_tool_plugin* p = nullptr;
        if (vinox_plugin_std_retrieval_create(storage, embedding, &p) == VINOX_STATUS_OK) {
            return ToolPlugin(p);
        }
        return ToolPlugin();
    }

    static ToolPlugin load_dynamic(const std::string& dll_path, const std::string& expected_sha256 = "") {
        vinox_tool_plugin* p = nullptr;
        if (vinox_plugin_load_dynamic(dll_path.c_str(), expected_sha256.empty() ? nullptr : expected_sha256.c_str(), &p) == VINOX_STATUS_OK) {
            return ToolPlugin(p);
        }
        return ToolPlugin();
    }

private:
    vinox_tool_plugin* plugin_;
};

} // namespace plugins
} // namespace vinox

#endif // VINOX_PLUGINS_HPP
