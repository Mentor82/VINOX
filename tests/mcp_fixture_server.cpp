#include <iostream>
#include <string>
#include <nlohmann/json.hpp>

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        try {
            auto req = nlohmann::json::parse(line);
            if (!req.contains("method") || !req["method"].is_string()) continue;

            std::string method = req["method"].get<std::string>();
            nlohmann::json res;
            res["jsonrpc"] = "2.0";

            if (req.contains("id")) {
                res["id"] = req["id"];
            }

            if (method == "initialize") {
                res["result"]["protocolVersion"] = "2024-11-05";
                res["result"]["capabilities"] = nlohmann::json::object();
                res["result"]["serverInfo"]["name"] = "vinox-mcp-fixture";
                res["result"]["serverInfo"]["version"] = "1.0.0";
            } else if (method == "notifications/initialized") {
                continue; // No response for notification
            } else if (method == "tools/list") {
                nlohmann::json tool;
                tool["name"] = "query";
                tool["description"] = "Execute SQL read query";
                tool["inputSchema"] = nlohmann::json::parse("{\"type\":\"object\",\"properties\":{\"sql\":{\"type\":\"string\"}},\"required\":[\"sql\"]}");
                res["result"]["tools"] = nlohmann::json::array({tool});
            } else if (method == "tools/call") {
                res["result"]["content"] = nlohmann::json::array({
                    {{"type", "text"}, {"text", "Executed query successfully"}}
                });
            } else if (method == "resources/list") {
                res["result"]["resources"] = nlohmann::json::array({
                    {{"uri", "vinox://sqlite/schema"}, {"name", "Schema"}}
                });
            } else if (method == "resources/read") {
                res["result"]["contents"] = nlohmann::json::array({
                    {{"uri", "vinox://sqlite/schema"}, {"text", "CREATE TABLE test;"}}
                });
            } else if (method == "prompts/list") {
                res["result"]["prompts"] = nlohmann::json::array({
                    {{"name", "sqlite_analysis"}, {"description", "Analyze data"}}
                });
            } else if (method == "prompts/get") {
                res["result"]["description"] = "Rendered analysis prompt";
            } else {
                res["error"]["code"] = -32601;
                res["error"]["message"] = "Method not found";
            }

            std::cout << res.dump() << "\n" << std::flush;
        } catch (...) {
            // Ignore parse errors on input
        }
    }
    return 0;
}
