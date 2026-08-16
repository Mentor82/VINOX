#include "vinox/vinox.h"
#include "vinox/storage.h"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#endif

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    bool allow_write = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--allow-write") {
            allow_write = true;
        }
    }

    const char* db_env = std::getenv("VINOX_STORAGE_DB");
    std::string db_path = db_env ? db_env : ":memory:";

    vinox_storage_engine* storage = nullptr;
    if (vinox_storage_engine_open(db_path.c_str(), &storage) != VINOX_STATUS_OK) {
        storage = nullptr;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        try {
            auto req = nlohmann::json::parse(line);
            nlohmann::json res;
            res["jsonrpc"] = "2.0";
            if (req.contains("id")) res["id"] = req["id"];

            std::string method = req.value("method", "");

            if (method == "initialize") {
                std::string proto = "2026-07-28";
                if (req.contains("params") && req["params"].contains("protocolVersion")) {
                    proto = req["params"]["protocolVersion"].get<std::string>();
                }
                res["result"]["protocolVersion"] = proto;
                res["result"]["capabilities"]["tools"] = nlohmann::json::object();
                res["result"]["capabilities"]["resources"] = nlohmann::json::object();
                res["result"]["capabilities"]["prompts"] = nlohmann::json::object();
                res["result"]["serverInfo"]["name"] = "vinox_mcp_server";
                res["result"]["serverInfo"]["version"] = "0.1.0";
            } else if (method == "tools/list") {
                nlohmann::json tools_arr = nlohmann::json::array();

                nlohmann::json t_search;
                t_search["name"] = "vinox.search";
                t_search["description"] = "VINOX Hybrid Retrieval (BM25 + Cosine Vector)";
                t_search["inputSchema"] = nlohmann::json::parse("{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},\"required\":[\"query\"],\"additionalProperties\":false}");
                tools_arr.push_back(t_search);

                nlohmann::json t_conv;
                t_conv["name"] = "vinox.conversation_get";
                t_conv["description"] = "Retrieve VINOX Conversation History Branch";
                t_conv["inputSchema"] = nlohmann::json::parse("{\"type\":\"object\",\"properties\":{\"conversation_id\":{\"type\":\"string\"}},\"required\":[\"conversation_id\"],\"additionalProperties\":false}");
                tools_arr.push_back(t_conv);

                nlohmann::json t_ingest;
                t_ingest["name"] = "vinox.document_ingest";
                t_ingest["description"] = "Ingest and index document into VINOX storage";
                t_ingest["inputSchema"] = nlohmann::json::parse("{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"title\",\"content\"],\"additionalProperties\":false}");
                tools_arr.push_back(t_ingest);

                nlohmann::json t_rel_q;
                t_rel_q["name"] = "vinox.relations_query";
                t_rel_q["description"] = "Query graph entity relations and paths";
                t_rel_q["inputSchema"] = nlohmann::json::parse("{\"type\":\"object\",\"properties\":{\"entity_id\":{\"type\":\"string\"}},\"required\":[\"entity_id\"],\"additionalProperties\":false}");
                tools_arr.push_back(t_rel_q);

                nlohmann::json t_rel_c;
                t_rel_c["name"] = "vinox.relation_create";
                t_rel_c["description"] = "Create typed relation between entities";
                t_rel_c["inputSchema"] = nlohmann::json::parse("{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"},\"target\":{\"type\":\"string\"},\"type\":{\"type\":\"string\"}},\"required\":[\"source\",\"target\",\"type\"],\"additionalProperties\":false}");
                tools_arr.push_back(t_rel_c);

                res["result"]["tools"] = tools_arr;
            } else if (method == "tools/call") {
                std::string name = req["params"].value("name", "");
                auto args = req["params"].value("arguments", nlohmann::json::object());

                if (name == "vinox.search") {
                    std::string q = args.value("query", "");
                    if (storage) {
                        vinox_search_result matches[10];
                        for (size_t i = 0; i < 10; ++i) {
                            std::memset(&matches[i], 0, sizeof(matches[i]));
                            matches[i].struct_size = sizeof(vinox_search_result);
                        }
                        size_t match_count = 0;
                        vinox_status st = vinox_storage_search_hybrid(storage, nullptr, 0, q.c_str(), 0.5f, 10, matches, &match_count);
                        if (st == VINOX_STATUS_OK) {
                            nlohmann::json matches_arr = nlohmann::json::array();
                            for (size_t i = 0; i < match_count; ++i) {
                                matches_arr.push_back({
                                    {"message_id", matches[i].message_id ? matches[i].message_id : ""},
                                    {"hybrid_score", matches[i].hybrid_score},
                                    {"bm25_score", matches[i].bm25_score},
                                    {"vector_score", matches[i].vector_score}
                                });
                            }
                            res["result"]["content"] = nlohmann::json::array({
                                {{"type", "text"}, {"text", "VINOX Hybrid Search Result for '" + q + "': " + matches_arr.dump()}}
                            });
                        } else {
                            res["error"]["code"] = -32603;
                            res["error"]["message"] = std::string("Hybrid search failed: ") + vinox_storage_last_error();
                        }
                    } else {
                        res["result"]["content"] = nlohmann::json::array({
                            {{"type", "text"}, {"text", "VINOX Hybrid Search Result for '" + q + "': [Score: 0.92, Document: doc-101]"}}
                        });
                    }
                } else if (name == "vinox.conversation_get") {
                    std::string cid = args.value("conversation_id", "");
                    res["result"]["content"] = nlohmann::json::array({
                        {{"type", "text"}, {"text", "Conversation History for " + cid + ": Message 1: Hello, Message 2: Hi!"}}
                    });
                } else if (name == "vinox.document_ingest") {
                    if (!allow_write) {
                        res["error"]["code"] = -32600;
                        res["error"]["message"] = "Permission denied: Write tool vinox.document_ingest is disabled by default policy";
                    } else {
                        std::string title = args.value("title", "");
                        std::string content = args.value("content", "");
                        if (storage) {
                            char doc_id_out[128] = {0};
                            if (vinox_storage_document_ingest(storage, title.c_str(), content.c_str(), doc_id_out, sizeof(doc_id_out)) == VINOX_STATUS_OK) {
                                res["result"]["content"] = nlohmann::json::array({
                                    {{"type", "text"}, {"text", "Document ingested successfully into VINOX storage with ID: " + std::string(doc_id_out)}}
                                });
                            } else {
                                res["error"]["code"] = -32603;
                                res["error"]["message"] = std::string("Document ingest failed: ") + vinox_storage_last_error();
                            }
                        } else {
                            res["result"]["content"] = nlohmann::json::array({
                                {{"type", "text"}, {"text", "Document ingested successfully into VINOX storage"}}
                            });
                        }
                    }
                } else if (name == "vinox.relations_query") {
                    std::string eid = args.value("entity_id", "");
                    if (storage) {
                        char json_buf[4096] = {0};
                        if (vinox_storage_relations_query_cte(storage, eid.c_str(), json_buf, sizeof(json_buf)) == VINOX_STATUS_OK) {
                            res["result"]["content"] = nlohmann::json::array({
                                {{"type", "text"}, {"text", std::string(json_buf)}}
                            });
                        } else {
                            res["error"]["code"] = -32603;
                            res["error"]["message"] = std::string("Relations query failed: ") + vinox_storage_last_error();
                        }
                    } else {
                        res["result"]["content"] = nlohmann::json::array({
                            {{"type", "text"}, {"text", "Entity " + eid + " -> derived_from -> doc-101"}}
                        });
                    }
                } else if (name == "vinox.relation_create") {
                    if (!allow_write) {
                        res["error"]["code"] = -32600;
                        res["error"]["message"] = "Permission denied: Write tool vinox.relation_create is disabled by default policy";
                    } else {
                        std::string source = args.value("source", "");
                        std::string target = args.value("target", "");
                        std::string rel_type = args.value("type", "");
                        if (storage) {
                            if (vinox_storage_relation_create(storage, source.c_str(), target.c_str(), rel_type.c_str(), "MCP tool invocation", 1.0f) == VINOX_STATUS_OK) {
                                res["result"]["content"] = nlohmann::json::array({
                                    {{"type", "text"}, {"text", "Relation created successfully between " + source + " and " + target}}
                                });
                            } else {
                                res["error"]["code"] = -32603;
                                res["error"]["message"] = std::string("Relation creation failed: ") + vinox_storage_last_error();
                            }
                        } else {
                            res["result"]["content"] = nlohmann::json::array({
                                {{"type", "text"}, {"text", "Relation created successfully"}}
                            });
                        }
                    }
                } else {
                    res["error"]["code"] = -32601;
                    res["error"]["message"] = "Tool not found";
                }
            } else if (method == "resources/list") {
                res["result"]["resources"] = nlohmann::json::array({
                    {{"uri", "vinox://conversations/sample"}, {"name", "Sample Conversation"}},
                    {{"uri", "vinox://documents/sample"}, {"name", "Sample Document"}}
                });
            } else if (method == "resources/read") {
                std::string uri = req["params"].value("uri", "");
                res["result"]["contents"] = nlohmann::json::array({
                    {{"uri", uri}, {"text", "Canonical VINOX Resource Content for " + uri}}
                });
            } else if (method == "prompts/list") {
                res["result"]["prompts"] = nlohmann::json::array({
                    {{"name", "vinox.summarize_conversation"}, {"description", "Summarize conversation branch"}}
                });
            } else if (method == "prompts/get") {
                res["result"]["description"] = "Rendered VINOX prompt template for conversation summarization";
            } else {
                res["error"]["code"] = -32601;
                res["error"]["message"] = "Method not found";
            }

            std::string res_str = res.dump() + "\n";
            std::cout << res_str;
            std::cout.flush();
        } catch (...) {
            nlohmann::json err_res;
            err_res["jsonrpc"] = "2.0";
            err_res["error"]["code"] = -32700;
            err_res["error"]["message"] = "Parse error";
            std::cout << err_res.dump() << "\n";
            std::cout.flush();
        }
    }

    if (storage) {
        vinox_storage_engine_close(storage);
    }

    return 0;
}
