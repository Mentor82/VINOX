#include "vinox/vinox.h"
#include "vinox/storage.h"
#include "vinox/tools.h"
#include "vinox/logging.h"
#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <future>
#include <chrono>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#endif

namespace fs = std::filesystem;

struct CanonicalToolSpec {
    const char* name;
    const char* description;
    const char* schema_json;
    vinox_security_class security_class;
};

static const CanonicalToolSpec CANONICAL_TOOLS[] = {
    {
        "vinox.search",
        "VINOX Hybrid Retrieval (BM25 FTS5 Text Search + Optional 1024-dim Cosine Vector Search)",
        "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"embedding\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"description\":\"Optional 1024-dim dense float embedding vector\"},\"timeout_sim_ms\":{\"type\":\"integer\"},\"timeout_limit_ms\":{\"type\":\"integer\"},\"oversize_sim_kb\":{\"type\":\"integer\"},\"cancelled\":{\"type\":\"boolean\"}},\"required\":[\"query\"],\"additionalProperties\":false}",
        VINOX_SECURITY_CLASS_READ_ONLY
    },
    {
        "vinox.conversation_get",
        "Retrieve VINOX Conversation History Branch",
        "{\"type\":\"object\",\"properties\":{\"conversation_id\":{\"type\":\"string\"},\"leaf_message_id\":{\"type\":\"string\"}},\"required\":[\"conversation_id\"],\"additionalProperties\":false}",
        VINOX_SECURITY_CLASS_READ_ONLY
    },
    {
        "vinox.document_ingest",
        "Ingest and index document into VINOX storage",
        "{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"title\",\"content\"],\"additionalProperties\":false}",
        VINOX_SECURITY_CLASS_LOCAL_WRITE
    },
    {
        "vinox.relations_query",
        "Query graph entity relations and paths",
        "{\"type\":\"object\",\"properties\":{\"entity_id\":{\"type\":\"string\"}},\"required\":[\"entity_id\"],\"additionalProperties\":false}",
        VINOX_SECURITY_CLASS_READ_ONLY
    },
    {
        "vinox.relation_create",
        "Create typed relation between entities",
        "{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"},\"target\":{\"type\":\"string\"},\"type\":{\"type\":\"string\"}},\"required\":[\"source\",\"target\",\"type\"],\"additionalProperties\":false}",
        VINOX_SECURITY_CLASS_LOCAL_WRITE
    }
};
static const size_t CANONICAL_TOOLS_COUNT = sizeof(CANONICAL_TOOLS) / sizeof(CANONICAL_TOOLS[0]);

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
    std::string storage_err_msg;
    if (vinox_storage_engine_open(db_path.c_str(), &storage) != VINOX_STATUS_OK || !storage) {
        storage = nullptr;
        storage_err_msg = vinox_storage_last_error();
        if (storage_err_msg.empty()) storage_err_msg = "Failed to open SQLite database engine at " + db_path;
    }

    // Fail-Closed Governance Control Loop: Registry & Policy Engine Setup
    vinox_tool_registry* registry = nullptr;
    vinox_policy_engine* policy_engine = nullptr;
    bool governance_initialized = false;

    const char* fail_reg_env = std::getenv("VINOX_TEST_FAIL_REGISTRY");
    bool force_reg_fail = (fail_reg_env && std::string(fail_reg_env) == "1");

    if (!force_reg_fail &&
        vinox_tool_registry_create(&registry) == VINOX_STATUS_OK && registry &&
        vinox_policy_engine_create(&policy_engine) == VINOX_STATUS_OK && policy_engine) {
        
        bool all_registered = true;
        for (size_t i = 0; i < CANONICAL_TOOLS_COUNT; ++i) {
            vinox_tool_definition tdef;
            std::memset(&tdef, 0, sizeof(tdef));
            tdef.struct_size = sizeof(tdef);
            tdef.name = CANONICAL_TOOLS[i].name;
            tdef.description = CANONICAL_TOOLS[i].description;
            tdef.parameters_json_schema = CANONICAL_TOOLS[i].schema_json;
            tdef.security_class = CANONICAL_TOOLS[i].security_class;

            if (vinox_tool_registry_register_tool(registry, &tdef) != VINOX_STATUS_OK) {
                all_registered = false;
                break;
            }
        }

        if (all_registered) {
            if (allow_write) {
                vinox_policy_engine_set_rule(policy_engine, "*", VINOX_SECURITY_CLASS_LOCAL_WRITE, VINOX_APPROVAL_AUTO_ALLOWED);
            } else {
                vinox_policy_engine_set_rule(policy_engine, "*", VINOX_SECURITY_CLASS_READ_ONLY, VINOX_APPROVAL_AUTO_ALLOWED);
            }
            governance_initialized = true;
        }
    }

    auto make_backend_error = [&](nlohmann::json& res) {
        res["result"]["isError"] = true;
        res["result"]["content"] = nlohmann::json::array({
            {{"type", "text"}, {"text", "VINOX storage backend unavailable: " + storage_err_msg}}
        });
    };

    auto make_governance_error = [&](nlohmann::json& res) {
        res["result"]["isError"] = true;
        res["result"]["content"] = nlohmann::json::array({
            {{"type", "text"}, {"text", "VINOX tool governance engine unavailable: initialization failed"}}
        });
    };

    auto log_gov_event = [&](const char* event_id, const std::string& details, uint64_t dur_ms, const char* status_str, uint32_t status_code) {
        vinox_log_event_meta meta;
        std::memset(&meta, 0, sizeof(meta));
        meta.struct_size = sizeof(meta);
        meta.duration_ms = dur_ms;
        meta.status = status_str;
        meta.status_code = status_code;
        meta.details = details.c_str();
        vinox_log_event_ex(VINOX_LOG_INFO, "mcp_server", event_id, nullptr, &meta);
    };

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
                if (!governance_initialized) {
                    res["error"]["code"] = -32603;
                    res["error"]["message"] = "VINOX tool governance engine unavailable: initialization failed";
                } else {
                    nlohmann::json tools_arr = nlohmann::json::array();
                    for (size_t i = 0; i < CANONICAL_TOOLS_COUNT; ++i) {
                        nlohmann::json t_item;
                        t_item["name"] = CANONICAL_TOOLS[i].name;
                        t_item["description"] = CANONICAL_TOOLS[i].description;
                        t_item["inputSchema"] = nlohmann::json::parse(CANONICAL_TOOLS[i].schema_json);
                        tools_arr.push_back(t_item);
                    }
                    res["result"]["tools"] = tools_arr;
                }
            } else if (method == "tools/call") {
                if (!storage) {
                    make_backend_error(res);
                } else if (!governance_initialized) {
                    make_governance_error(res);
                } else {
                    std::string name = req["params"].value("name", "");
                    auto args = req["params"].value("arguments", nlohmann::json::object());
                    std::string args_str = args.dump();

                    // Check Cancellation Gate
                    bool is_cancelled = (args.contains("cancelled") && args["cancelled"].is_boolean() && args["cancelled"].get<bool>()) ||
                                        (req.contains("params") && req["params"].contains("cancelled") && req["params"]["cancelled"].get<bool>());
                    if (is_cancelled) {
                        log_gov_event("tool.execution_completion", "Tool execution cancelled", 0, "CANCELLED", 0);
                        res["result"]["isError"] = true;
                        res["result"]["content"] = nlohmann::json::array({
                            {{"type", "text"}, {"text", "Tool execution cancelled"}}
                        });
                        std::string res_str = res.dump() + "\n";
                        std::cout << res_str;
                        std::cout.flush();
                        continue;
                    }

                    // Gate 1: Bounded Input Payload Size Limit Check (Max 128 KB)
                    if (args_str.size() > 131072) {
                        log_gov_event("tool.validation", "Input payload size limit exceeded", 0, "INVALID_ARGUMENTS", VINOX_STATUS_INVALID_ARGUMENT);
                        res["result"]["isError"] = true;
                        res["result"]["content"] = nlohmann::json::array({
                            {{"type", "text"}, {"text", "Input payload size limit exceeded (max 128 KB)"}}
                        });
                        std::string res_str = res.dump() + "\n";
                        std::cout << res_str;
                        std::cout.flush();
                        continue;
                    }

                    // Gate 2: Registry Lookup & Registered Tool Definition Binding
                    char pool_buf[4096] = {0};
                    vinox_tool_definition tdef;
                    std::memset(&tdef, 0, sizeof(tdef));
                    tdef.struct_size = sizeof(tdef);
                    if (vinox_tool_registry_find_tool(registry, name.c_str(), &tdef, pool_buf, sizeof(pool_buf)) != VINOX_STATUS_OK) {
                        log_gov_event("tool.lookup", "Tool not found: " + name, 0, "NOT_FOUND", VINOX_STATUS_NOT_FOUND);
                        res["error"]["code"] = -32601;
                        res["error"]["message"] = "Tool not found: " + name;
                        std::string res_str = res.dump() + "\n";
                        std::cout << res_str;
                        std::cout.flush();
                        continue;
                    }

                    // Gate 3: Central Phase 6.1 Bounded Schema Validator Check
                    char val_err[512] = {0};
                    vinox_status val_st = vinox_tool_registry_validate_arguments(registry, name.c_str(), args_str.c_str(), val_err, sizeof(val_err));
                    if (val_st != VINOX_STATUS_OK) {
                        log_gov_event("tool.validation", "Invalid arguments: " + std::string(val_err), 0, "INVALID_ARGUMENTS", val_st);
                        res["result"]["isError"] = true;
                        res["result"]["content"] = nlohmann::json::array({
                            {{"type", "text"}, {"text", "Invalid tool arguments: " + std::string(val_err)}}
                        });
                        std::string res_str = res.dump() + "\n";
                        std::cout << res_str;
                        std::cout.flush();
                        continue;
                    }
                    log_gov_event("tool.validation", "Tool arguments validated", 0, "OK", VINOX_STATUS_OK);

                    // Gate 4: Central Phase 6.1 C-ABI Policy Engine Authorization Check
                    vinox_tool_call_request req_call;
                    std::memset(&req_call, 0, sizeof(req_call));
                    req_call.struct_size = sizeof(req_call);
                    req_call.call_id = "mcp_call";
                    req_call.tool_name = name.c_str();
                    req_call.arguments_json = args_str.c_str();

                    vinox_policy_decision pdecision;
                    std::memset(&pdecision, 0, sizeof(pdecision));
                    pdecision.struct_size = sizeof(pdecision);
                    char reason_buf[512] = {0};

                    if (vinox_policy_engine_evaluate(policy_engine, &req_call, &tdef, &pdecision, reason_buf, sizeof(reason_buf)) != VINOX_STATUS_OK || !pdecision.allowed) {
                        log_gov_event("tool.policy_decision", "Policy denied: " + std::string(reason_buf), 0, "DENIED", VINOX_STATUS_PERMISSION_DENIED);
                        res["result"]["isError"] = true;
                        res["result"]["content"] = nlohmann::json::array({
                            {{"type", "text"}, {"text", "Permission denied: Tool execution rejected by policy engine: " + std::string(reason_buf)}}
                        });
                        std::string res_str = res.dump() + "\n";
                        std::cout << res_str;
                        std::cout.flush();
                        continue;
                    }
                    log_gov_event("tool.policy_decision", "Policy allowed: " + std::string(reason_buf), 0, "ALLOWED", VINOX_STATUS_OK);

                    // Gate 5: Execution Start & Async Deadline / Timeout Control
                    log_gov_event("tool.execution_start", "Tool execution starting: " + name, 0, "OK", VINOX_STATUS_OK);
                    auto start_time = std::chrono::steady_clock::now();
                    uint32_t timeout_limit_ms = args.value("timeout_limit_ms", 2000);

                    // Execute tool handler in async task
                    auto task_fut = std::async(std::launch::async, [&]() -> nlohmann::json {
                        nlohmann::json inner_res = nlohmann::json::object();

                        // Simulated execution delay for timeout testing
                        if (args.contains("timeout_sim_ms") && args["timeout_sim_ms"].is_number_integer()) {
                            int sim_ms = args["timeout_sim_ms"].get<int>();
                            std::this_thread::sleep_for(std::chrono::milliseconds(sim_ms));
                        }

                        // Simulated oversize output testing
                        if (args.contains("oversize_sim_kb") && args["oversize_sim_kb"].is_number_integer()) {
                            int sim_kb = args["oversize_sim_kb"].get<int>();
                            std::string huge_text(sim_kb * 1024, 'X');
                            inner_res["result"]["content"] = nlohmann::json::array({
                                {{"type", "text"}, {"text", huge_text}}
                            });
                            return inner_res;
                        }

                        if (name == "vinox.search") {
                            std::string q = args.value("query", "");
                            std::vector<float> query_vec;
                            if (args.contains("embedding")) {
                                if (!args["embedding"].is_array()) {
                                    inner_res["result"]["isError"] = true;
                                    inner_res["result"]["content"] = nlohmann::json::array({
                                        {{"type", "text"}, {"text", "Invalid embedding parameter: expected JSON array of numbers"}}
                                    });
                                } else {
                                    for (const auto& item : args["embedding"]) {
                                        if (!item.is_number()) {
                                            inner_res["result"]["isError"] = true;
                                            inner_res["result"]["content"] = nlohmann::json::array({
                                                {{"type", "text"}, {"text", "Invalid embedding element: all array items must be numbers"}}
                                            });
                                            break;
                                        }
                                        query_vec.push_back(item.get<float>());
                                    }
                                    if (!inner_res["result"].contains("isError") && !query_vec.empty() && query_vec.size() != 1024) {
                                        inner_res["result"]["isError"] = true;
                                        inner_res["result"]["content"] = nlohmann::json::array({
                                            {{"type", "text"}, {"text", "Invalid embedding dimension: expected 1024 float values, got " + std::to_string(query_vec.size())}}
                                        });
                                    }
                                }
                            }

                            if (!inner_res["result"].contains("isError")) {
                                const float* emb_ptr = query_vec.empty() ? nullptr : query_vec.data();
                                size_t emb_dim = query_vec.size();
                                float alpha = query_vec.empty() ? 0.0f : 0.5f;

                                vinox_search_result matches[10];
                                for (size_t i = 0; i < 10; ++i) {
                                    std::memset(&matches[i], 0, sizeof(matches[i]));
                                    matches[i].struct_size = sizeof(vinox_search_result);
                                }
                                size_t match_count = 0;
                                vinox_status st = vinox_storage_search_hybrid(storage, emb_ptr, emb_dim, q.c_str(), alpha, 10, matches, &match_count);
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
                                    inner_res["result"]["content"] = nlohmann::json::array({
                                        {{"type", "text"}, {"text", "VINOX Hybrid Search Result for '" + q + "': " + matches_arr.dump()}}
                                    });
                                } else {
                                    inner_res["result"]["isError"] = true;
                                    inner_res["result"]["content"] = nlohmann::json::array({
                                        {{"type", "text"}, {"text", "Hybrid search failed: " + std::string(vinox_storage_last_error())}}
                                    });
                                }
                            }
                        } else if (name == "vinox.conversation_get") {
                            std::string cid = args.value("conversation_id", "");
                            std::string target_msg_id = args.value("leaf_message_id", "");
                            static char json_buf[524288] = {0};
                            size_t req_sz = 0;
                            if (vinox_storage_export_json(storage, json_buf, sizeof(json_buf), &req_sz) == VINOX_STATUS_OK) {
                                auto root = nlohmann::json::parse(json_buf);
                                std::unordered_map<std::string, nlohmann::json> msg_map;
                                std::unordered_set<std::string> parent_ids;
                                std::vector<nlohmann::json> conv_msgs;

                                if (root.contains("messages") && root["messages"].is_array()) {
                                    for (const auto& m : root["messages"]) {
                                        if (m.value("conversation_id", "") == cid) {
                                            std::string mid = m.value("id", "");
                                            msg_map[mid] = m;
                                            if (m.contains("parent_id") && !m["parent_id"].is_null()) {
                                                parent_ids.insert(m["parent_id"].get<std::string>());
                                            }
                                            conv_msgs.push_back(m);
                                        }
                                    }
                                }

                                if (conv_msgs.empty()) {
                                    inner_res["result"]["isError"] = true;
                                    inner_res["result"]["content"] = nlohmann::json::array({
                                        {{"type", "text"}, {"text", "Conversation not found or has no messages: " + cid}}
                                    });
                                } else if (!target_msg_id.empty() && msg_map.find(target_msg_id) == msg_map.end()) {
                                    inner_res["result"]["isError"] = true;
                                    inner_res["result"]["content"] = nlohmann::json::array({
                                        {{"type", "text"}, {"text", "Specified leaf_message_id not found: " + target_msg_id}}
                                    });
                                } else {
                                    std::string leaf_id = target_msg_id;
                                    if (leaf_id.empty()) {
                                        for (const auto& m : conv_msgs) {
                                            std::string mid = m.value("id", "");
                                            if (parent_ids.find(mid) == parent_ids.end()) {
                                                leaf_id = mid;
                                                break;
                                            }
                                        }
                                        if (leaf_id.empty()) leaf_id = conv_msgs.back().value("id", "");
                                    }

                                    std::vector<nlohmann::json> branch_chain;
                                    std::unordered_set<std::string> visited;
                                    std::string curr_id = leaf_id;
                                    while (!curr_id.empty() && msg_map.find(curr_id) != msg_map.end() && visited.find(curr_id) == visited.end()) {
                                        visited.insert(curr_id);
                                        branch_chain.push_back(msg_map[curr_id]);
                                        auto curr_msg = msg_map[curr_id];
                                        if (curr_msg.contains("parent_id") && !curr_msg["parent_id"].is_null()) {
                                            curr_id = curr_msg["parent_id"].get<std::string>();
                                        } else {
                                            break;
                                        }
                                    }
                                    std::reverse(branch_chain.begin(), branch_chain.end());

                                    inner_res["result"]["content"] = nlohmann::json::array({
                                        {{"type", "text"}, {"text", "Conversation History Branch for " + cid + ": " + nlohmann::json(branch_chain).dump()}}
                                    });
                                }
                            } else {
                                inner_res["result"]["isError"] = true;
                                inner_res["result"]["content"] = nlohmann::json::array({
                                    {{"type", "text"}, {"text", "Failed to retrieve conversation history: " + std::string(vinox_storage_last_error())}}
                                });
                            }
                        } else if (name == "vinox.document_ingest") {
                            std::string title = args.value("title", "");
                            std::string content = args.value("content", "");
                            char doc_id_out[128] = {0};
                            if (vinox_storage_document_ingest(storage, title.c_str(), content.c_str(), doc_id_out, sizeof(doc_id_out)) == VINOX_STATUS_OK) {
                                inner_res["result"]["content"] = nlohmann::json::array({
                                    {{"type", "text"}, {"text", "Document ingested successfully into VINOX storage with ID: " + std::string(doc_id_out)}}
                                });
                            } else {
                                inner_res["result"]["isError"] = true;
                                inner_res["result"]["content"] = nlohmann::json::array({
                                    {{"type", "text"}, {"text", "Document ingest failed: " + std::string(vinox_storage_last_error())}}
                                });
                            }
                        } else if (name == "vinox.relations_query") {
                            std::string eid = args.value("entity_id", "");
                            char json_buf[4096] = {0};
                            if (vinox_storage_relations_query_cte(storage, eid.c_str(), json_buf, sizeof(json_buf)) == VINOX_STATUS_OK) {
                                inner_res["result"]["content"] = nlohmann::json::array({
                                    {{"type", "text"}, {"text", std::string(json_buf)}}
                                });
                            } else {
                                inner_res["result"]["isError"] = true;
                                inner_res["result"]["content"] = nlohmann::json::array({
                                    {{"type", "text"}, {"text", "Relations query failed: " + std::string(vinox_storage_last_error())}}
                                });
                            }
                        } else if (name == "vinox.relation_create") {
                            std::string source = args.value("source", "");
                            std::string target = args.value("target", "");
                            std::string rel_type = args.value("type", "");
                            if (vinox_storage_relation_create(storage, source.c_str(), target.c_str(), rel_type.c_str(), "MCP tool invocation", 1.0f) == VINOX_STATUS_OK) {
                                inner_res["result"]["content"] = nlohmann::json::array({
                                    {{"type", "text"}, {"text", "Relation created successfully between " + source + " and " + target}}
                                });
                            } else {
                                inner_res["result"]["isError"] = true;
                                inner_res["result"]["content"] = nlohmann::json::array({
                                    {{"type", "text"}, {"text", "Relation creation failed: " + std::string(vinox_storage_last_error())}}
                                });
                            }
                        }

                        return inner_res;
                    });

                    if (task_fut.wait_for(std::chrono::milliseconds(timeout_limit_ms)) == std::future_status::timeout) {
                        log_gov_event("tool.execution_completion", "Tool execution timed out after " + std::to_string(timeout_limit_ms) + " ms", timeout_limit_ms, "TIMEOUT", VINOX_STATUS_RUNTIME_ERROR);
                        res["result"]["isError"] = true;
                        res["result"]["content"] = nlohmann::json::array({
                            {{"type", "text"}, {"text", "Tool execution timed out after " + std::to_string(timeout_limit_ms) + " ms"}}
                        });
                    } else {
                        auto task_res = task_fut.get();
                        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
                        res["result"] = task_res["result"];

                        // Gate 6: Bounded Output Payload Size Limit Check (Max 256 KB)
                        std::string out_dump = res["result"].dump();
                        if (out_dump.size() > 262144) {
                            log_gov_event("tool.execution_completion", "Tool execution output payload size exceeded", elapsed_ms, "OVERSIZE_OUTPUT", VINOX_STATUS_RUNTIME_ERROR);
                            res["result"]["isError"] = true;
                            res["result"]["content"] = nlohmann::json::array({
                                {{"type", "text"}, {"text", "Tool execution result exceeded maximum output payload size limit (256 KB)"}}
                            });
                        } else {
                            log_gov_event("tool.execution_completion", "Tool execution completed successfully", elapsed_ms, "OK", VINOX_STATUS_OK);
                        }
                    }
                }
            } else if (method == "resources/list") {
                if (!storage) {
                    res["error"]["code"] = -32603;
                    res["error"]["message"] = "VINOX storage backend unavailable: " + storage_err_msg;
                } else {
                    static char json_buf[524288] = {0};
                    size_t req_sz = 0;
                    nlohmann::json res_arr = nlohmann::json::array();
                    if (vinox_storage_export_json(storage, json_buf, sizeof(json_buf), &req_sz) == VINOX_STATUS_OK) {
                        auto root = nlohmann::json::parse(json_buf);
                        if (root.contains("conversations") && root["conversations"].is_array()) {
                            for (const auto& c : root["conversations"]) {
                                std::string cid = c.value("id", "");
                                std::string ctitle = c.value("title", "Conversation " + cid);
                                res_arr.push_back({{"uri", "vinox://conversations/" + cid}, {"name", ctitle}});
                            }
                        }
                        if (root.contains("documents") && root["documents"].is_array()) {
                            for (const auto& d : root["documents"]) {
                                std::string did = d.value("id", "");
                                std::string dtitle = d.value("title", "Document " + did);
                                res_arr.push_back({{"uri", "vinox://documents/" + did}, {"name", dtitle}});
                            }
                        }
                    }
                    res["result"]["resources"] = res_arr;
                }
            } else if (method == "resources/read") {
                if (!storage) {
                    res["error"]["code"] = -32603;
                    res["error"]["message"] = "VINOX storage backend unavailable: " + storage_err_msg;
                } else {
                    std::string uri = req["params"].value("uri", "");
                    static char json_buf[524288] = {0};
                    size_t req_sz = 0;
                    bool found = false;
                    std::string content_text;
                    if (vinox_storage_export_json(storage, json_buf, sizeof(json_buf), &req_sz) == VINOX_STATUS_OK) {
                        auto root = nlohmann::json::parse(json_buf);
                        if (uri.rfind("vinox://conversations/", 0) == 0) {
                            std::string cid = uri.substr(strlen("vinox://conversations/"));
                            nlohmann::json conv_msgs = nlohmann::json::array();
                            if (root.contains("messages") && root["messages"].is_array()) {
                                for (const auto& m : root["messages"]) {
                                    if (m.value("conversation_id", "") == cid) {
                                        conv_msgs.push_back(m);
                                    }
                                }
                            }
                            if (!conv_msgs.empty()) {
                                found = true;
                                content_text = "Conversation Resource " + cid + ": " + conv_msgs.dump();
                            }
                        } else if (uri.rfind("vinox://documents/", 0) == 0) {
                            std::string did = uri.substr(strlen("vinox://documents/"));
                            if (root.contains("documents") && root["documents"].is_array()) {
                                for (const auto& d : root["documents"]) {
                                    if (d.value("id", "") == did) {
                                        found = true;
                                        content_text = "Document Resource " + did + ": " + d.dump();
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (found) {
                        res["result"]["contents"] = nlohmann::json::array({
                            {{"uri", uri}, {"text", content_text}}
                        });
                    } else {
                        res["error"]["code"] = -32602;
                        res["error"]["message"] = "Resource not found for URI: " + uri;
                    }
                }
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
    if (registry) {
        vinox_tool_registry_destroy(registry);
    }
    if (policy_engine) {
        vinox_policy_engine_destroy(policy_engine);
    }

    return 0;
}
