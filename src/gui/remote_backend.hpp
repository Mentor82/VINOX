#pragma once

#include "backend_interface.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace vinox::gui {

class RemoteVinoxBackend : public IVinoxBackend {
public:
    explicit RemoteVinoxBackend(const std::string& host = "127.0.0.1", int port = 8080, const std::string& api_key = "");
    ~RemoteVinoxBackend() override;

    bool is_connected() const override;
    std::string backend_name() const override { return "Remote (HTTP/SSE)"; }

    std::vector<ModelInfo> list_models() override;
    bool load_model(const std::string& model_id, const std::string& device) override;
    bool unload_model(const std::string& model_id) override;

    bool generate_chat_stream(
        const std::vector<ChatMessage>& history,
        float temperature,
        float top_p,
        uint64_t max_tokens,
        StreamTokenCallback on_token,
        std::string& out_error,
        const std::string& conversation_id = ""
    ) override;
    void cancel_generation() override;

    std::vector<std::pair<std::string, std::string>> list_conversations() override;
    std::vector<ConversationInfo> list_conversations_detailed() override;
    std::string create_conversation(const std::string& title) override;
    std::vector<ChatMessage> get_conversation_messages(const std::string& conversation_id) override;
    bool delete_conversation(const std::string& conversation_id) override;
    std::vector<SearchMatch> search_hybrid(const std::string& query, float alpha, uint32_t limit) override;
    std::vector<EntityRelation> get_relations(const std::string& source_id) override;

    PlanData create_plan(const std::string& task) override;
    bool approve_plan(const std::string& plan_id, const std::string& expected_plan_hash) override;

    std::string start_agent_run(
        const std::string& plan_id,
        const std::string& plan_hash,
        const std::string& workspace_dir,
        AgentEventCallback on_event
    ) override;
    AgentRunStatus get_agent_run_status(const std::string& run_id) override;
    bool cancel_agent_run(const std::string& run_id) override;

    DiffArtifact get_pending_diff(const std::string& run_id) override;
    bool apply_diff(const std::string& run_id, const std::string& base_snapshot_hash, const std::vector<uint32_t>& selected_hunk_ids) override;

private:
    std::string host_;
    int port_;
    std::string api_key_;
    std::atomic<bool> cancel_requested_{false};
};

} // namespace vinox::gui
