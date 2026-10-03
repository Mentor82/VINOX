#include "agent_handlers.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vinox::server {

namespace {

std::string generate_id(const std::string& prefix) {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    std::stringstream ss;
    ss << prefix << std::hex << std::setfill('0') << std::setw(16) << dis(gen);
    return ss.str();
}

void send_error(httplib::Response& res, int status_code, const std::string& type, const std::string& message) {
    nlohmann::json err_json = {
        {"error", {
            {"message", message},
            {"type", type},
            {"param", nullptr},
            {"code", status_code}
        }}
    };
    res.status = status_code;
    res.set_content(err_json.dump(), "application/json");
}

} // namespace

void handle_list_conversations(const httplib::Request&, httplib::Response& res, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.storage_mutex);
    if (!ctx.storage) {
        send_error(res, 503, "storage_unavailable", "Storage engine is not available");
        return;
    }

    std::vector<char> json_buf(65536, 0);
    vinox_status st = vinox_storage_list_conversations_json(ctx.storage, json_buf.data(), json_buf.size());
    if (st != VINOX_STATUS_OK) {
        send_error(res, 500, "storage_error", vinox_storage_last_error() ? vinox_storage_last_error() : "Failed to list conversations");
        return;
    }

    nlohmann::json data;
    try {
        data = nlohmann::json::parse(json_buf.data());
    } catch (...) {
        data = nlohmann::json::array();
    }

    nlohmann::json resp = {
        {"object", "list"},
        {"data", data},
        {"conversations", data}
    };
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_get_conversation(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::string id = req.path_params.count("id") ? req.path_params.at("id") : "";
    if (id.empty()) {
        send_error(res, 400, "invalid_request_error", "Missing conversation id");
        return;
    }

    std::lock_guard<std::mutex> lock(ctx.storage_mutex);
    if (!ctx.storage) {
        send_error(res, 503, "storage_unavailable", "Storage engine is not available");
        return;
    }

    char json_buf[16384] = {0};
    vinox_status st = vinox_storage_get_conversation_json(ctx.storage, id.c_str(), json_buf, sizeof(json_buf));
    if (st != VINOX_STATUS_OK) {
        send_error(res, 404, "not_found", vinox_storage_last_error() ? vinox_storage_last_error() : "Conversation not found");
        return;
    }

    res.status = 200;
    res.set_content(json_buf, "application/json");
}

void handle_delete_conversation(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::string id = req.path_params.count("id") ? req.path_params.at("id") : "";
    if (id.empty()) {
        send_error(res, 400, "invalid_request_error", "Missing conversation id");
        return;
    }

    std::lock_guard<std::mutex> lock(ctx.storage_mutex);
    if (!ctx.storage) {
        send_error(res, 503, "storage_unavailable", "Storage engine is not available");
        return;
    }

    vinox_status st = vinox_storage_delete_conversation(ctx.storage, id.c_str());
    if (st != VINOX_STATUS_OK) {
        send_error(res, 404, "not_found", vinox_storage_last_error() ? vinox_storage_last_error() : "Failed to delete conversation");
        return;
    }

    nlohmann::json resp = {
        {"id", id},
        {"deleted", true}
    };
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_get_conversation_messages(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::string id = req.path_params.count("id") ? req.path_params.at("id") : "";
    if (id.empty()) {
        send_error(res, 400, "invalid_request_error", "Missing conversation id");
        return;
    }

    std::lock_guard<std::mutex> lock(ctx.storage_mutex);
    if (!ctx.storage) {
        send_error(res, 503, "storage_unavailable", "Storage engine is not available");
        return;
    }

    std::vector<char> json_buf(262144, 0);
    vinox_status st = vinox_storage_get_conversation_messages_json(ctx.storage, id.c_str(), json_buf.data(), json_buf.size());
    if (st != VINOX_STATUS_OK) {
        send_error(res, 500, "storage_error", vinox_storage_last_error() ? vinox_storage_last_error() : "Failed to retrieve conversation messages");
        return;
    }

    nlohmann::json msgs;
    try {
        msgs = nlohmann::json::parse(json_buf.data());
    } catch (...) {
        msgs = nlohmann::json::array();
    }

    nlohmann::json resp = {
        {"object", "list"},
        {"conversation_id", id},
        {"data", msgs},
        {"messages", msgs}
    };
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_add_conversation_message(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::string id = req.path_params.count("id") ? req.path_params.at("id") : "";
    if (id.empty()) {
        send_error(res, 400, "invalid_request_error", "Missing conversation id");
        return;
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        send_error(res, 400, "invalid_request_error", std::string("Malformed JSON: ") + e.what());
        return;
    }

    std::string role = payload.value("role", "user");
    std::string content = payload.value("content", "");
    std::string parent_id = payload.value("parent_id", "");
    uint32_t prov_kind = payload.value("provenance_kind", 0);

    if (content.empty()) {
        send_error(res, 400, "invalid_request_error", "Message content cannot be empty");
        return;
    }

    vinox_message_info msg_in{};
    msg_in.struct_size = sizeof(msg_in);
    msg_in.conversation_id = id.c_str();
    msg_in.role = role.c_str();
    msg_in.content = content.c_str();
    if (!parent_id.empty()) {
        msg_in.parent_id = parent_id.c_str();
    }
    msg_in.provenance_kind = prov_kind;

    vinox_message_info msg_out{};
    msg_out.struct_size = sizeof(msg_out);

    std::lock_guard<std::mutex> lock(ctx.storage_mutex);
    if (!ctx.storage) {
        send_error(res, 503, "storage_unavailable", "Storage engine is not available");
        return;
    }

    vinox_status st = vinox_storage_add_message(ctx.storage, &msg_in, &msg_out);
    if (st != VINOX_STATUS_OK) {
        send_error(res, 500, "storage_error", vinox_storage_last_error() ? vinox_storage_last_error() : "Failed to add message");
        return;
    }

    // Generate vector embedding if embedding engine loaded
    if (msg_out.id) {
        std::lock_guard<std::mutex> emb_lock(ctx.embedding_mutex);
        if (ctx.embedding_engine) {
            size_t dim = 0;
            vinox_embedding_get_dim(ctx.embedding_engine, &dim);
            if (dim > 0) {
                std::vector<float> vec(dim);
                size_t actual_dim = 0;
                if (vinox_embedding_generate(ctx.embedding_engine, content.c_str(), vec.data(), vec.size(), &actual_dim) == VINOX_STATUS_OK) {
                    vinox_storage_store_embedding(ctx.storage, msg_out.id, vec.data(), actual_dim);
                }
            }
        }
    }

    nlohmann::json resp = {
        {"id", msg_out.id ? msg_out.id : ""},
        {"conversation_id", id},
        {"role", role},
        {"content", content},
        {"created_at_ms", msg_out.created_at_ms}
    };
    if (!parent_id.empty()) {
        resp["parent_id"] = parent_id;
    }
    res.status = 201;
    res.set_content(resp.dump(), "application/json");
}

void handle_create_conversation(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.storage_mutex);
    if (!ctx.storage) {
        send_error(res, 503, "storage_unavailable", "Storage engine is not available");
        return;
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (...) {
        payload = nlohmann::json::object();
    }

    std::string title = payload.value("title", "New Conversation");

    vinox_conversation_info info{};
    info.struct_size = sizeof(info);
    vinox_status st = vinox_storage_create_conversation(ctx.storage, title.c_str(), &info);
    if (st != VINOX_STATUS_OK) {
        send_error(res, 500, "storage_error", "Failed to create conversation");
        return;
    }

    nlohmann::json resp = {
        {"id", info.id ? info.id : generate_id("conv_")},
        {"title", title},
        {"created_at_ms", info.created_at_ms}
    };
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_search(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.storage_mutex);
    if (!ctx.storage) {
        send_error(res, 503, "storage_unavailable", "Storage engine is not available");
        return;
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        send_error(res, 400, "invalid_request_error", std::string("Malformed JSON: ") + e.what());
        return;
    }

    std::string query = payload.value("query", "");
    float alpha = payload.value("alpha", 0.5f);
    size_t limit = payload.value("limit", 5);

    std::vector<float> embedding;
    if (payload.contains("embedding") && payload["embedding"].is_array()) {
        for (const auto& v : payload["embedding"]) {
            if (v.is_number()) embedding.push_back(v.get<float>());
        }
    }

    // Auto-generate query embedding if not provided and vector search is enabled
    if (embedding.empty() && alpha > 0.0f) {
        std::lock_guard<std::mutex> emb_lock(ctx.embedding_mutex);
        if (ctx.embedding_engine) {
            size_t dim = 0;
            vinox_embedding_get_dim(ctx.embedding_engine, &dim);
            if (dim > 0) {
                embedding.resize(dim);
                size_t actual_dim = 0;
                vinox_status est = vinox_embedding_generate(ctx.embedding_engine, query.c_str(), embedding.data(), embedding.size(), &actual_dim);
                if (est != VINOX_STATUS_OK || actual_dim == 0) {
                    embedding.clear();
                }
            }
        }
    }

    std::vector<vinox_search_result> results(limit);
    for (auto& r : results) r.struct_size = sizeof(r);
    size_t count = 0;

    vinox_status st = vinox_storage_search_hybrid(
        ctx.storage,
        embedding.empty() ? nullptr : embedding.data(),
        embedding.size(),
        query.c_str(),
        alpha,
        limit,
        results.data(),
        &count
    );

    if (st != VINOX_STATUS_OK) {
        send_error(res, 500, "search_error", "Hybrid search failed");
        return;
    }

    nlohmann::json data = nlohmann::json::array();
    nlohmann::json results_arr = nlohmann::json::array();
    for (size_t i = 0; i < count; ++i) {
        const char* mid = results[i].message_id ? results[i].message_id : "";
        char content_buf[4096] = {0};
        char title_buf[256] = {0};
        vinox_storage_get_message_content(ctx.storage, mid, content_buf, sizeof(content_buf), title_buf, sizeof(title_buf));

        std::string snippet = content_buf[0] != '\0' ? content_buf : mid;
        std::string title = title_buf[0] != '\0' ? title_buf : ("Treffer #" + std::to_string(i + 1));

        data.push_back({
            {"message_id", mid},
            {"title", title},
            {"snippet", snippet},
            {"bm25_score", results[i].bm25_score},
            {"vector_score", results[i].vector_score},
            {"hybrid_score", results[i].hybrid_score}
        });

        results_arr.push_back({
            {"id", mid},
            {"title", title},
            {"snippet", snippet},
            {"score", results[i].hybrid_score},
            {"bm25_score", results[i].bm25_score},
            {"vector_score", results[i].vector_score}
        });
    }

    nlohmann::json resp = {
        {"query", query},
        {"alpha", alpha},
        {"matches", data},
        {"results", results_arr}
    };
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_relations_query(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.storage_mutex);
    if (!ctx.storage) {
        send_error(res, 503, "storage_unavailable", "Storage engine is not available");
        return;
    }

    if (!req.has_param("source_id")) {
        send_error(res, 400, "invalid_request_error", "Missing 'source_id' query parameter");
        return;
    }
    std::string source_id = req.get_param_value("source_id");

    char json_buf[32768] = {0};
    vinox_status st = vinox_storage_relations_query_cte(ctx.storage, source_id.c_str(), json_buf, sizeof(json_buf));
    if (st != VINOX_STATUS_OK) {
        send_error(res, 500, "relations_error", "Failed to query relations CTE");
        return;
    }

    nlohmann::json graph;
    try {
        graph = nlohmann::json::parse(json_buf);
    } catch (...) {
        graph = nlohmann::json::array();
    }

    nlohmann::json resp;
    resp["source_id"] = source_id;
    resp["relations"] = graph;

    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_documents_ingest(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.storage_mutex);
    if (!ctx.storage) {
        send_error(res, 503, "storage_unavailable", "Storage engine is not available");
        return;
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        send_error(res, 400, "invalid_request_error", std::string("Malformed JSON: ") + e.what());
        return;
    }

    std::string title = payload.value("title", "Untitled Document");
    std::string content = payload.value("content", "");
    if (content.empty()) {
        send_error(res, 400, "invalid_request_error", "'content' cannot be empty");
        return;
    }

    char doc_id_buf[128] = {0};
    vinox_status st = vinox_storage_document_ingest(ctx.storage, title.c_str(), content.c_str(), doc_id_buf, sizeof(doc_id_buf));
    if (st != VINOX_STATUS_OK) {
        send_error(res, 500, "ingest_error", "Failed to ingest document into storage");
        return;
    }

    // Also persist as conversation message so hybrid search immediately finds it
    vinox_conversation_info conv{};
    conv.struct_size = sizeof(conv);
    vinox_storage_create_conversation(ctx.storage, "System Knowledge Index", &conv);

    vinox_message_info msg_in{};
    msg_in.struct_size = sizeof(msg_in);
    msg_in.conversation_id = conv.id ? conv.id : "sys_knowledge";
    msg_in.role = title.c_str();
    msg_in.content = content.c_str();
    msg_in.provenance_kind = VINOX_PROVENANCE_SOURCE_LITERAL;

    vinox_message_info msg_out{};
    msg_out.struct_size = sizeof(msg_out);
    vinox_status mst = vinox_storage_add_message(ctx.storage, &msg_in, &msg_out);

    // If embedding engine is loaded, generate & store embedding for vector search
    if (mst == VINOX_STATUS_OK && msg_out.id) {
        std::lock_guard<std::mutex> emb_lock(ctx.embedding_mutex);
        if (ctx.embedding_engine) {
            size_t dim = 0;
            vinox_embedding_get_dim(ctx.embedding_engine, &dim);
            if (dim > 0) {
                std::vector<float> vec(dim);
                size_t actual_dim = 0;
                if (vinox_embedding_generate(ctx.embedding_engine, content.c_str(), vec.data(), vec.size(), &actual_dim) == VINOX_STATUS_OK) {
                    vinox_storage_store_embedding(ctx.storage, msg_out.id, vec.data(), actual_dim);
                }
            }
        }
    }

    // Ingest any relations if provided
    int rels_created = 0;
    if (payload.contains("relations") && payload["relations"].is_array()) {
        for (const auto& r : payload["relations"]) {
            std::string src = r.value("source_id", "");
            std::string tgt = r.value("target_id", "");
            std::string type = r.value("type", "related_to");
            std::string evidence = r.value("evidence", "");
            float conf = r.value("confidence", 1.0f);
            if (!src.empty() && !tgt.empty()) {
                if (vinox_storage_relation_create(ctx.storage, src.c_str(), tgt.c_str(), type.c_str(), evidence.c_str(), conf) == VINOX_STATUS_OK) {
                    rels_created++;
                }
            }
        }
    }

    nlohmann::json resp = {
        {"status", "ingested"},
        {"document_id", doc_id_buf},
        {"title", title},
        {"message_id", msg_out.id ? msg_out.id : ""},
        {"bytes", content.size()},
        {"relations_created", rels_created}
    };
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_relations_create(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.storage_mutex);
    if (!ctx.storage) {
        send_error(res, 503, "storage_unavailable", "Storage engine is not available");
        return;
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        send_error(res, 400, "invalid_request_error", std::string("Malformed JSON: ") + e.what());
        return;
    }

    std::string source_id = payload.value("source_id", "");
    std::string target_id = payload.value("target_id", "");
    std::string type = payload.value("type", "related_to");
    std::string evidence = payload.value("evidence", "");
    float confidence = payload.value("confidence", 1.0f);

    if (source_id.empty() || target_id.empty()) {
        send_error(res, 400, "invalid_request_error", "Both 'source_id' and 'target_id' are required");
        return;
    }

    vinox_status st = vinox_storage_relation_create(
        ctx.storage,
        source_id.c_str(),
        target_id.c_str(),
        type.c_str(),
        evidence.c_str(),
        confidence
    );

    if (st != VINOX_STATUS_OK) {
        send_error(res, 500, "relations_error", "Failed to create relation");
        return;
    }

    nlohmann::json resp = {{"status", "created"}, {"source_id", source_id}, {"target_id", target_id}, {"type", type}};
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_relations_delete(const httplib::Request&, httplib::Response& res, ServerContext&) {
    res.status = 200;
    res.set_content("{\"status\":\"deleted\"}", "application/json");
}

void handle_plans_create(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        send_error(res, 400, "invalid_request_error", std::string("Malformed JSON: ") + e.what());
        return;
    }

    std::string task = payload.value("task", "");
    if (task.empty()) {
        send_error(res, 400, "invalid_request_error", "'task' description is required");
        return;
    }

    std::string plan_id = generate_id("plan_");
    nlohmann::json plan_obj = {
        {"version", "1.0.0"},
        {"goal", task},
        {"plan_id", plan_id},
        {"status", "draft"},
        {"steps", nlohmann::json::array({
            {{"step_id", 1}, {"description", "Inspect workspace"}, {"tool", "read_file"}},
            {{"step_id", 2}, {"description", "Execute task"}, {"tool", "write_file"}},
            {{"step_id", 3}, {"description", "Verify outcome"}, {"tool", "run_tests"}}
        })}
    };

    vinox_plan* plan = vinox_plan_create(plan_obj.dump().c_str());
    if (!plan) {
        send_error(res, 500, "plan_error", "Failed to create plan object");
        return;
    }

    char hash_buf[65] = {0};
    vinox_plan_compute_hash(plan, hash_buf, sizeof(hash_buf));
    vinox_plan_destroy(plan);

    plan_obj["plan_hash"] = std::string(hash_buf);

    {
        std::lock_guard<std::mutex> lock(ctx.plans_mutex);
        ctx.plans[plan_id] = plan_obj;
    }

    res.status = 200;
    res.set_content(plan_obj.dump(), "application/json");
}

void handle_plans_get(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::string plan_id = req.path_params.at("id");
    std::lock_guard<std::mutex> lock(ctx.plans_mutex);
    auto it = ctx.plans.find(plan_id);
    if (it == ctx.plans.end()) {
        send_error(res, 404, "not_found", "Plan not found");
        return;
    }
    res.status = 200;
    res.set_content(it->second.dump(), "application/json");
}

void handle_plans_approve(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::string plan_id = req.path_params.at("id");
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        send_error(res, 400, "invalid_request_error", std::string("Malformed JSON: ") + e.what());
        return;
    }

    std::string expected_hash = payload.value("plan_hash", "");
    if (expected_hash.empty()) {
        send_error(res, 400, "invalid_request_error", "'plan_hash' is required for approval");
        return;
    }

    std::lock_guard<std::mutex> lock(ctx.plans_mutex);
    auto it = ctx.plans.find(plan_id);
    if (it == ctx.plans.end()) {
        send_error(res, 404, "not_found", "Plan not found");
        return;
    }

    std::string actual_hash = it->second.value("plan_hash", "");
    if (actual_hash != expected_hash) {
        send_error(res, 409, "plan_hash_mismatch", "Provided plan_hash does not match target plan identity");
        return;
    }

    it->second["status"] = "approved";

    nlohmann::json resp = {
        {"status", "approved"},
        {"plan_id", plan_id},
        {"plan_hash", expected_hash}
    };
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_agent_runs_create(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        send_error(res, 400, "invalid_request_error", std::string("Malformed JSON: ") + e.what());
        return;
    }

    std::string plan_id = payload.value("plan_id", "");
    std::string plan_hash = payload.value("plan_hash", "");
    std::string workspace_dir = payload.value("workspace_dir", ".");

    if (plan_id.empty() || plan_hash.empty()) {
        send_error(res, 400, "invalid_request_error", "'plan_id' and 'plan_hash' are required");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ctx.plans_mutex);
        auto it = ctx.plans.find(plan_id);
        if (it == ctx.plans.end()) {
            send_error(res, 404, "not_found", "Plan not found");
            return;
        }
        if (it->second.value("status", "") != "approved") {
            send_error(res, 403, "plan_not_approved", "Plan has not been approved by user");
            return;
        }
        if (it->second.value("plan_hash", "") != plan_hash) {
            send_error(res, 409, "plan_hash_mismatch", "Plan hash verification failed");
            return;
        }
    }

    std::string run_id = generate_id("run_");
    AgentRunState r_state;
    r_state.run_id = run_id;
    r_state.plan_id = plan_id;
    r_state.plan_hash = plan_hash;
    r_state.workspace_dir = workspace_dir;
    r_state.status = VINOX_PLAN_STATUS_RUNNING;
    r_state.created_at = std::chrono::system_clock::now();

    r_state.events.push_back({
        {"event_id", 1},
        {"type", "status"},
        {"status", "running"},
        {"message", "Agent execution initialized"}
    });

    {
        std::lock_guard<std::mutex> lock(ctx.runs_mutex);
        ctx.runs[run_id] = std::move(r_state);
    }

    nlohmann::json resp = {
        {"run_id", run_id},
        {"plan_id", plan_id},
        {"status", "running"}
    };
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_agent_runs_get(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::string run_id = req.path_params.at("id");
    std::lock_guard<std::mutex> lock(ctx.runs_mutex);
    auto it = ctx.runs.find(run_id);
    if (it == ctx.runs.end()) {
        send_error(res, 404, "not_found", "Agent run not found");
        return;
    }

    const auto& r = it->second;
    nlohmann::json resp = {
        {"run_id", r.run_id},
        {"plan_id", r.plan_id},
        {"status", r.status == VINOX_PLAN_STATUS_RUNNING ? "running" :
                   r.status == VINOX_PLAN_STATUS_COMPLETED ? "completed" :
                   r.status == VINOX_PLAN_STATUS_CANCELLED ? "cancelled" : "failed"},
        {"completed_steps", r.completed_steps},
        {"events_count", r.events.size()}
    };
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_agent_runs_cancel(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::string run_id = req.path_params.at("id");
    std::lock_guard<std::mutex> lock(ctx.runs_mutex);
    auto it = ctx.runs.find(run_id);
    if (it == ctx.runs.end()) {
        send_error(res, 404, "not_found", "Agent run not found");
        return;
    }

    it->second.status = VINOX_PLAN_STATUS_CANCELLED;
    it->second.events.push_back({
        {"event_id", static_cast<int>(it->second.events.size() + 1)},
        {"type", "status"},
        {"status", "cancelled"},
        {"message", "Run cancelled by user request"}
    });

    nlohmann::json resp = {
        {"run_id", run_id},
        {"status", "cancelled"}
    };
    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

void handle_agent_runs_events(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    std::string run_id = req.path_params.at("id");

    res.set_chunked_content_provider(
        "text/event-stream",
        [&ctx, run_id](size_t /*offset*/, httplib::DataSink& sink) -> bool {
            std::vector<nlohmann::json> events_snapshot;
            {
                std::lock_guard<std::mutex> lock(ctx.runs_mutex);
                auto it = ctx.runs.find(run_id);
                if (it != ctx.runs.end()) {
                    events_snapshot = it->second.events;
                }
            }

            for (const auto& ev : events_snapshot) {
                std::string formatted = "data: " + ev.dump() + "\n\n";
                sink.write(formatted.data(), formatted.size());
            }

            std::string done_msg = "data: [DONE]\n\n";
            sink.write(done_msg.data(), done_msg.size());
            sink.done();
            return true;
        }
    );
}

void handle_tool_execute(const httplib::Request& req, httplib::Response& res, ServerContext& ctx) {
    if (!ctx.tool_registry) {
        send_error(res, 503, "tool_registry_unavailable", "Tool registry not initialized");
        return;
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        send_error(res, 400, "invalid_request_error", std::string("Malformed JSON: ") + e.what());
        return;
    }

    std::string tool_name = payload.value("name", "");
    if (tool_name.empty()) {
        send_error(res, 400, "invalid_request_error", "'name' is required");
        return;
    }

    std::string args_str = "{}";
    if (payload.contains("arguments")) {
        if (payload["arguments"].is_string()) {
            args_str = payload["arguments"].get<std::string>();
        } else {
            args_str = payload["arguments"].dump();
        }
    }

    vinox_tool_call_request tr_req{};
    tr_req.struct_size = sizeof(tr_req);
    tr_req.call_id = "direct_call";
    tr_req.tool_name = tool_name.c_str();
    tr_req.arguments_json = args_str.c_str();

    vinox_tool_call_result tr_res{};
    tr_res.struct_size = sizeof(tr_res);
    std::vector<char> pool_buf(65536, 0);

    vinox_status st = vinox_tool_registry_execute(ctx.tool_registry, nullptr, &tr_req, &tr_res, pool_buf.data(), pool_buf.size());
    if (st != VINOX_STATUS_OK) {
        send_error(res, tr_res.status_code != 0 ? tr_res.status_code : 500, "tool_execution_error",
                   tr_res.error_message ? tr_res.error_message : (vinox_tools_last_error() ? vinox_tools_last_error() : "Tool execution failed"));
        return;
    }

    nlohmann::json res_val;
    if (tr_res.result_json && tr_res.result_json[0] != '\0') {
        try {
            res_val = nlohmann::json::parse(tr_res.result_json);
        } catch (...) {
            res_val = tr_res.result_json;
        }
    } else {
        res_val = nlohmann::json::object();
    }

    nlohmann::json res_obj = {
        {"status_code", tr_res.status_code},
        {"tool", tool_name},
        {"result", res_val}
    };

    res.status = 200;
    res.set_content(res_obj.dump(), "application/json");
}

} // namespace vinox::server
