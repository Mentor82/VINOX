#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <atomic>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#endif

std::atomic<bool> g_stop_server{false};

void run_http_server(int port) {
#if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
        listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listen_sock);
        WSACleanup();
        return;
    }

    while (!g_stop_server.load()) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listen_sock, &fds);
        timeval tv{1, 0}; // 1 second timeout
        int sel = select(0, &fds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        SOCKET client_sock = accept(listen_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) continue;

        char buf[4096];
        int bytes = recv(client_sock, buf, sizeof(buf) - 1, 0);
        if (bytes <= 0) {
            closesocket(client_sock);
            continue;
        }
        buf[bytes] = '\0';
        std::string raw_req(buf, bytes);

        std::istringstream stream(raw_req);
        std::string req_line;
        std::getline(stream, req_line);

        std::string method, path, proto;
        std::istringstream line_stream(req_line);
        line_stream >> method >> path >> proto;

        size_t qpos = path.find('?');
        if (qpos != std::string::npos) path = path.substr(0, qpos);

        std::string proto_ver_hdr, mcp_method_hdr, mcp_name_hdr;
        std::string header_line;

        while (std::getline(stream, header_line) && header_line != "\r" && !header_line.empty()) {
            if (!header_line.empty() && header_line.back() == '\r') header_line.pop_back();
            size_t colon = header_line.find(':');
            if (colon != std::string::npos) {
                std::string k = header_line.substr(0, colon);
                std::string v = header_line.substr(colon + 1);
                while (!v.empty() && v.front() == ' ') v.erase(0, 1);

                if (k == "Mcp-Protocol-Version") proto_ver_hdr = v;
                else if (k == "Mcp-Method") mcp_method_hdr = v;
                else if (k == "Mcp-Name") mcp_name_hdr = v;
            }
        }

        size_t body_pos = raw_req.find("\r\n\r\n");
        std::string body_str = (body_pos != std::string::npos) ? raw_req.substr(body_pos + 4) : "";

        std::string http_res;
        if (path == "/bad_header") {
            http_res = "HTTP/1.1 400 Bad Request\r\nContent-Length: 22\r\n\r\nHeader validation failed";
        } else if (path == "/sse" && method == "GET") {
            std::string sse_body = "event: endpoint\r\ndata: /messages?session_id=sse_123\r\n\r\n";
            http_res = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: " + std::to_string(sse_body.length()) + "\r\n\r\n" + sse_body;
        } else {
            nlohmann::json res_json;
            res_json["jsonrpc"] = "2.0";

            try {
                auto req_json = nlohmann::json::parse(body_str);
                if (req_json.contains("id")) res_json["id"] = req_json["id"];

                std::string mcp_method = req_json.value("method", mcp_method_hdr);

                if (mcp_method == "initialize") {
                    res_json["result"]["protocolVersion"] = proto_ver_hdr.empty() ? "2024-11-05" : proto_ver_hdr;
                    res_json["result"]["capabilities"] = nlohmann::json::object();
                    res_json["result"]["serverInfo"]["name"] = "vinox-http-fixture";
                    res_json["result"]["serverInfo"]["version"] = "1.0.0";
                } else if (mcp_method == "tools/list") {
                    nlohmann::json t1;
                    t1["name"] = "query";
                    t1["description"] = "HTTP SQL read query";
                    t1["inputSchema"] = nlohmann::json::parse("{\"type\":\"object\",\"properties\":{\"sql\":{\"type\":\"string\"}},\"required\":[\"sql\"]}");
                    res_json["result"]["tools"] = nlohmann::json::array({t1});
                } else if (mcp_method == "tools/call") {
                    res_json["result"]["content"] = nlohmann::json::array({
                        {{"type", "text"}, {"text", "Executed HTTP query successfully"}}
                    });
                } else if (mcp_method == "resources/list") {
                    res_json["result"]["resources"] = nlohmann::json::array({
                        {{"uri", "vinox://http_sqlite/schema"}, {"name", "HTTP Schema"}}
                    });
                } else if (mcp_method == "resources/read") {
                    res_json["result"]["contents"] = nlohmann::json::array({
                        {{"uri", "vinox://http_sqlite/schema"}, {"text", "CREATE TABLE http_test;"}}
                    });
                } else if (mcp_method == "prompts/list") {
                    res_json["result"]["prompts"] = nlohmann::json::array({
                        {{"name", "http_sqlite_analysis"}, {"description", "HTTP Analysis"}}
                    });
                } else if (mcp_method == "prompts/get") {
                    res_json["result"]["description"] = "Rendered HTTP analysis prompt";
                } else {
                    res_json["error"]["code"] = -32601;
                    res_json["error"]["message"] = "Method not found";
                }
            } catch (...) {
                res_json["error"]["code"] = -32700;
                res_json["error"]["message"] = "Parse error";
            }

            std::string res_body = res_json.dump();
            http_res = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(res_body.length()) + "\r\n\r\n" + res_body;
        }

        send(client_sock, http_res.c_str(), static_cast<int>(http_res.length()), 0);
        closesocket(client_sock);
    }

    closesocket(listen_sock);
    WSACleanup();
#endif
}

int main(int argc, char* argv[]) {
    int port = 18080;
    if (argc > 1) port = std::atoi(argv[1]);

    run_http_server(port);
    return 0;
}
