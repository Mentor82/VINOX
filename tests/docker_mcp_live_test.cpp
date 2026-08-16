#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include "vinox/mcp.hpp"
#include "vinox/tools.hpp"

int main() {
    std::cout << "========================================================\n";
    std::cout << "   VINOX <-> DOCKER MCP GATEWAY LIVE INTEGRATION TEST   \n";
    std::cout << "========================================================\n\n";

    bool all_passed = true;

    // 1. Test Streamable HTTP connection to Docker MCP Gateway (port 18090) if active
    std::cout << "[1/2] Connecting to Docker MCP Gateway over HTTP (http://localhost:18090/mcp)...\n";
    vinox::mcp::McpClient http_client("docker_gateway", VINOX_MCP_TRANSPORT_STREAMABLE_HTTP, "http://localhost:18090/mcp", VINOX_MCP_VERSION_2024_11_05);

    vinox_status st = http_client.connect();
    if (st != VINOX_STATUS_OK) {
        std::cout << "  INFO: Docker MCP Gateway HTTP server not running on port 18090 or connection refused.\n";
    } else {
        std::cout << "  SUCCESS: Connected to Docker MCP Gateway over HTTP!\n";

        vinox::tools::ToolRegistry reg;
        st = http_client.list_tools(reg);
        if (st != VINOX_STATUS_OK) {
            std::cerr << "  FAILED: Could not list tools from Docker MCP Gateway over HTTP: " << vinox_mcp_last_error() << "\n";
            all_passed = false;
        } else {
            std::cout << "  SUCCESS: Discovered tools from Docker MCP Gateway over HTTP!\n";

            char pool[4096];
            vinox_tool_definition tool_def{};
            tool_def.struct_size = sizeof(tool_def);

            if (vinox_tool_registry_find_tool(reg.get(), "docker_gateway.mcp-find", &tool_def, pool, sizeof(pool)) == VINOX_STATUS_OK) {
                std::cout << "  SUCCESS: Found namespaced Docker MCP Tool: '" << tool_def.name << "'\n";
            } else {
                std::cout << "  SUCCESS: Listed tools from HTTP gateway!\n";
            }
        }
    }

    // 2. Test Stdio connection to Docker MCP Gateway ('docker mcp gateway run')
    std::cout << "\n[2/2] Connecting to Docker MCP Gateway over Stdio ('docker mcp gateway run')...\n";
    vinox::mcp::McpClient stdio_client("docker_gateway_stdio", VINOX_MCP_TRANSPORT_STDIO, "docker mcp gateway run", VINOX_MCP_VERSION_2024_11_05);

    st = stdio_client.connect();
    if (st != VINOX_STATUS_OK) {
        std::cerr << "  FAILED: Could not connect to Docker MCP Gateway over stdio: " << vinox_mcp_last_error() << "\n";
        all_passed = false;
    } else {
        std::cout << "  SUCCESS: Connected to Docker MCP Gateway over stdio!\n";

        vinox::tools::ToolRegistry reg_stdio;
        st = stdio_client.list_tools(reg_stdio);
        if (st != VINOX_STATUS_OK) {
            std::cerr << "  FAILED: Could not list tools from Docker MCP Gateway over stdio: " << vinox_mcp_last_error() << "\n";
            all_passed = false;
        } else {
            std::cout << "  SUCCESS: Discovered tools from Docker MCP Gateway over stdio!\n";
        }
    }

    std::cout << "\n========================================================\n";
    if (all_passed) {
        std::cout << "   DOCKER MCP GATEWAY LIVE INTEGRATION VERIFIED 🟢      \n";
        std::cout << "========================================================\n";
        return 0;
    } else {
        std::cerr << "   DOCKER MCP GATEWAY LIVE INTEGRATION FAILED 🔴        \n";
        std::cout << "========================================================\n";
        return 1;
    }
}
