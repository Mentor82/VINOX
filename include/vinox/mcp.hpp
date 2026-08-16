#ifndef VINOX_MCP_HPP
#define VINOX_MCP_HPP

#include <memory>
#include <string>
#include <vector>
#include "vinox/mcp.h"
#include "vinox/tools.hpp"

namespace vinox {
namespace mcp {

class McpClient {
public:
    McpClient(
        const std::string& server_name,
        vinox_mcp_transport_kind transport_kind,
        const std::string& command_or_url,
        vinox_mcp_protocol_version protocol_ver = VINOX_MCP_VERSION_2026_07_28
    ) : client_(nullptr) {
        vinox_mcp_server_config config{};
        config.struct_size = sizeof(config);
        config.server_name = server_name.c_str();
        config.transport_kind = static_cast<uint32_t>(transport_kind);
        config.protocol_version = static_cast<uint32_t>(protocol_ver);
        config.command_or_url = command_or_url.c_str();
        config.legacy_handshake_enabled = (protocol_ver == VINOX_MCP_VERSION_2024_11_05) ? 1 : 0;
        config.legacy_sse_enabled = (transport_kind == VINOX_MCP_TRANSPORT_LEGACY_SSE) ? 1 : 0;

        vinox_mcp_client_create(&config, &client_);
    }

    ~McpClient() {
        if (client_) {
            vinox_mcp_client_destroy(client_);
            client_ = nullptr;
        }
    }

    McpClient(const McpClient&) = delete;
    McpClient& operator=(const McpClient&) = delete;

    McpClient(McpClient&& other) noexcept : client_(other.client_) {
        other.client_ = nullptr;
    }

    McpClient& operator=(McpClient&& other) noexcept {
        if (this != &other) {
            if (client_) vinox_mcp_client_destroy(client_);
            client_ = other.client_;
            other.client_ = nullptr;
        }
        return *this;
    }

    vinox_mcp_client* get() const { return client_; }

    vinox_status connect() {
        return vinox_mcp_client_connect(client_);
    }

    vinox_status disconnect() {
        return vinox_mcp_client_disconnect(client_);
    }

    bool is_connected() const {
        return vinox_mcp_client_is_connected(client_) != 0;
    }

    vinox_status list_tools(tools::ToolRegistry& registry) {
        return vinox_mcp_client_list_tools(client_, registry.get());
    }

private:
    vinox_mcp_client* client_;
};

} // namespace mcp
} // namespace vinox

#endif // VINOX_MCP_HPP
