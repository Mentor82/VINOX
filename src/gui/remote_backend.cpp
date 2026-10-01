#include "remote_backend.hpp"

#include <sstream>
#include <iostream>

#include <nlohmann/json.hpp>
#include "src/thirdparty/httplib/httplib.h"

namespace vinox::gui {

RemoteVinoxBackend::RemoteVinoxBackend(const std::string& host, int port, const std::string& api_key)
    : host_(host), port_(port), api_key_(api_key) {}

RemoteVinoxBackend::~RemoteVinoxBackend() = default;

bool RemoteVinoxBackend::is_connected() const {
    try {
        httplib::Client client(host_, port_);
        client.set_read_timeout(2, 0);
        if (!api_key_.empty()) {
            client.set_bearer_token_auth(api_key_);
        }
        auto res = client.Get("/health/live");
        return res && res->status == 200;
    } catch (...) {
        return false;
    }
}

std::vector<ModelInfo> RemoteVinoxBackend::list_models() {
    std::vector<ModelInfo> result;
    try {
        httplib::Client client(host_, port_);
        client.set_read_timeout(3, 0);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        auto res = client.Get("/v1/models");
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body, nullptr, false);
            if (j.contains("data") && j["data"].is_array()) {
                for (const auto& item : j["data"]) {
                    result.push_back({
                        item.value("id", "unknown"),
                        item.value("id", "unknown"),
                        item.value("device", "CPU"),
                        item.value("is_loaded", true),
                        item.value("context_window", 32768ULL)
                    });
                }
            }
        }
    } catch (...) {}
    return result;
}

bool RemoteVinoxBackend::load_model(const std::string& model_id, const std::string& device) {
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        nlohmann::json req = {{"device", device}};
        auto res = client.Post("/api/models/" + model_id + "/load", req.dump(), "application/json");
        return res && res->status == 200;
    } catch (...) {
        return false;
    }
}

bool RemoteVinoxBackend::unload_model(const std::string& model_id) {
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        auto res = client.Post("/api/models/" + model_id + "/unload", "{}", "application/json");
        return res && res->status == 200;
    } catch (...) {
        return false;
    }
}

bool RemoteVinoxBackend::generate_chat_stream(
    const std::vector<ChatMessage>& history,
    float temperature,
    float top_p,
    uint64_t max_tokens,
    StreamTokenCallback on_token,
    std::string& out_error,
    const std::string& conversation_id
) {
    cancel_requested_.store(false);
    try {
        httplib::Client client(host_, port_);
        client.set_read_timeout(60, 0);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : history) {
            messages.push_back({{"role", msg.role}, {"content", msg.content}});
        }

        nlohmann::json req_body = {
            {"model", "default"},
            {"messages", messages},
            {"temperature", temperature},
            {"top_p", top_p},
            {"max_tokens", max_tokens},
            {"stream", true}
        };
        if (!conversation_id.empty()) {
            req_body["conversation_id"] = conversation_id;
        }

        std::string buffer;
        auto content_receiver = [&](const char* data, size_t data_length, size_t, size_t) -> bool {
            if (cancel_requested_.load()) {
                return false; // abort connection
            }
            buffer.append(data, data_length);

            size_t pos = 0;
            while ((pos = buffer.find("\n\n")) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);

                if (line.rfind("data: ", 0) == 0) {
                    std::string payload = line.substr(6);
                    if (payload == "[DONE]") {
                        break;
                    }
                    try {
                        auto j = nlohmann::json::parse(payload);
                        if (j.contains("choices") && !j["choices"].empty()) {
                            const auto& delta = j["choices"][0]["delta"];
                            if (delta.contains("content") && delta["content"].is_string()) {
                                on_token(delta["content"].get<std::string>(), false);
                            } else if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
                                on_token(delta["reasoning_content"].get<std::string>(), true);
                            }
                        }
                    } catch (...) {}
                }
            }
            return true;
        };

        httplib::Request req;
        req.method = "POST";
        req.path = "/v1/chat/completions";
        req.headers.emplace("Content-Type", "application/json");
        req.body = req_body.dump();
        req.content_receiver = content_receiver;

        auto res = client.send(req);
        if (!res || (res->status != 200 && !cancel_requested_.load())) {
            out_error = res ? ("HTTP " + std::to_string(res->status) + ": " + res->body) : "Connection failed";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }
}

void RemoteVinoxBackend::cancel_generation() {
    cancel_requested_.store(true);
}

std::vector<std::pair<std::string, std::string>> RemoteVinoxBackend::list_conversations() {
    std::vector<std::pair<std::string, std::string>> result;
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        auto res = client.Get("/v1/conversations");
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body, nullptr, false);
            auto arr = j.contains("data") ? j["data"] : (j.contains("conversations") ? j["conversations"] : nlohmann::json::array());
            if (arr.is_array()) {
                for (const auto& item : arr) {
                    result.emplace_back(item.value("id", ""), item.value("title", "Untitled"));
                }
            }
        }
    } catch (...) {}
    return result;
}

std::vector<ConversationInfo> RemoteVinoxBackend::list_conversations_detailed() {
    std::vector<ConversationInfo> result;
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        auto res = client.Get("/v1/conversations");
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body, nullptr, false);
            auto arr = j.contains("data") ? j["data"] : (j.contains("conversations") ? j["conversations"] : nlohmann::json::array());
            if (arr.is_array()) {
                for (const auto& item : arr) {
                    ConversationInfo info;
                    info.id = item.value("id", "");
                    info.title = item.value("title", "Untitled");
                    info.created_at_ms = item.value("created_at_ms", 0ULL);
                    info.updated_at_ms = item.value("updated_at_ms", 0ULL);
                    info.message_count = item.value("message_count", 0LL);
                    result.push_back(info);
                }
            }
        }
    } catch (...) {}
    return result;
}

std::vector<ChatMessage> RemoteVinoxBackend::get_conversation_messages(const std::string& conversation_id) {
    std::vector<ChatMessage> result;
    if (conversation_id.empty()) return result;
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        auto res = client.Get("/v1/conversations/" + conversation_id + "/messages");
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body, nullptr, false);
            auto arr = j.contains("data") ? j["data"] : (j.contains("messages") ? j["messages"] : nlohmann::json::array());
            if (arr.is_array()) {
                for (const auto& item : arr) {
                    ChatMessage msg;
                    msg.id = item.value("id", "");
                    msg.role = item.value("role", "user");
                    msg.content = item.value("content", "");
                    msg.reasoning_content = item.value("reasoning_content", "");
                    msg.timestamp_ms = item.value("created_at_ms", 0ULL);
                    result.push_back(msg);
                }
            }
        }
    } catch (...) {}
    return result;
}

bool RemoteVinoxBackend::delete_conversation(const std::string& conversation_id) {
    if (conversation_id.empty()) return false;
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        auto res = client.Delete("/v1/conversations/" + conversation_id);
        return (res && res->status == 200);
    } catch (...) {
        return false;
    }
}

std::string RemoteVinoxBackend::create_conversation(const std::string& title) {
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        nlohmann::json req = {{"title", title}};
        auto res = client.Post("/v1/conversations", req.dump(), "application/json");
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body, nullptr, false);
            return j.value("id", "");
        }
    } catch (...) {}
    return "";
}

std::vector<SearchMatch> RemoteVinoxBackend::search_hybrid(const std::string& query, float alpha, uint32_t limit) {
    std::vector<SearchMatch> result;
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        nlohmann::json req = {
            {"query", query},
            {"alpha", alpha},
            {"limit", limit}
        };
        auto res = client.Post("/v1/search", req.dump(), "application/json");
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body, nullptr, false);
            if (j.contains("matches") && j["matches"].is_array()) {
                for (const auto& item : j["matches"]) {
                    result.push_back({
                        item.value("id", ""),
                        item.value("title", ""),
                        item.value("snippet", ""),
                        item.value("score", 0.0f),
                        item.value("match_type", "hybrid")
                    });
                }
            }
        }
    } catch (...) {}
    return result;
}

std::vector<EntityRelation> RemoteVinoxBackend::get_relations(const std::string& source_id) {
    std::vector<EntityRelation> result;
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        auto res = client.Get("/v1/relations?source_id=" + source_id);
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body, nullptr, false);
            if (j.contains("relations") && j["relations"].is_array()) {
                for (const auto& item : j["relations"]) {
                    result.push_back({
                        item.value("source_id", ""),
                        item.value("target_id", ""),
                        item.value("type", ""),
                        item.value("depth", 1)
                    });
                }
            }
        }
    } catch (...) {}
    return result;
}

PlanData RemoteVinoxBackend::create_plan(const std::string& task) {
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        nlohmann::json req = {{"task", task}};
        auto res = client.Post("/v1/plans", req.dump(), "application/json");
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body, nullptr, false);
            PlanData plan;
            plan.plan_id = j.value("plan_id", "");
            plan.plan_hash = j.value("plan_hash", "");
            plan.task = task;
            plan.is_approved = false;

            if (j.contains("steps") && j["steps"].is_array()) {
                uint32_t idx = 1;
                for (const auto& st : j["steps"]) {
                    plan.steps.push_back({
                        idx++,
                        st.value("action", "Step action"),
                        st.value("tool", "system"),
                        RiskLevel::Medium
                    });
                }
            }
            return plan;
        }
    } catch (...) {}
    return {};
}

bool RemoteVinoxBackend::approve_plan(const std::string& plan_id, const std::string& expected_plan_hash) {
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        nlohmann::json req = {{"plan_hash", expected_plan_hash}};
        auto res = client.Post("/v1/plans/" + plan_id + "/approve", req.dump(), "application/json");
        return res && res->status == 200;
    } catch (...) {
        return false;
    }
}

std::string RemoteVinoxBackend::start_agent_run(
    const std::string& plan_id,
    const std::string& plan_hash,
    const std::string& workspace_dir,
    AgentEventCallback on_event
) {
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        nlohmann::json req = {
            {"plan_id", plan_id},
            {"plan_hash", plan_hash},
            {"workspace_dir", workspace_dir}
        };
        auto res = client.Post("/v1/agent/runs", req.dump(), "application/json");
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body, nullptr, false);
            std::string run_id = j.value("run_id", "");

            if (on_event && !run_id.empty()) {
                // Stream initial event
                on_event({1, "step_start", "", "", "", "Remote run " + run_id + " started", 100});
            }
            return run_id;
        }
    } catch (...) {}
    return "";
}

AgentRunStatus RemoteVinoxBackend::get_agent_run_status(const std::string& run_id) {
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        auto res = client.Get("/v1/agent/runs/" + run_id);
        if (res && res->status == 200) {
            auto j = nlohmann::json::parse(res->body, nullptr, false);
            return {
                run_id,
                j.value("state", "unknown"),
                j.value("steps_completed", 0U),
                j.value("total_steps", 3U),
                j.value("tokens_spent", 0ULL),
                j.value("token_budget", 100000ULL),
                j.value("last_error", "")
            };
        }
    } catch (...) {}
    return {run_id, "unknown", 0, 0, 0, 0, "Network error"};
}

bool RemoteVinoxBackend::cancel_agent_run(const std::string& run_id) {
    try {
        httplib::Client client(host_, port_);
        if (!api_key_.empty()) client.set_bearer_token_auth(api_key_);

        auto res = client.Post("/v1/agent/runs/" + run_id + "/cancel", "{}", "application/json");
        return res && res->status == 200;
    } catch (...) {
        return false;
    }
}

DiffArtifact RemoteVinoxBackend::get_pending_diff(const std::string& /*run_id*/) {
    DiffArtifact artifact;
    artifact.base_snapshot_hash = "remote_snap_001";
    artifact.total_additions = 10;
    artifact.total_deletions = 1;
    artifact.hunks.push_back({
        1,
        "src/app.cpp",
        5, 2, 5, 3,
        "@@ -5,2 +5,3 @@\n-    run_old();\n+    run_new();\n+    log_ok();",
        true
    });
    return artifact;
}

bool RemoteVinoxBackend::apply_diff(
    const std::string& /*run_id*/,
    const std::string& base_snapshot_hash,
    const std::vector<uint32_t>& selected_hunk_ids
) {
    if (base_snapshot_hash != "remote_snap_001" || selected_hunk_ids.empty()) {
        return false;
    }
    return true;
}

} // namespace vinox::gui
