#pragma once

#include "backend_interface.hpp"

#include <mutex>
#include <atomic>
#include <map>

#include <vinox/vinox.h>
#include <vinox/openvino.h>
#include <vinox/embedding.h>
#include <vinox/plugins.h>
#include <vinox/serving.h>
#include <vinox/storage.h>
#include <vinox/vinox_agent.h>

namespace vinox::gui {

class LocalVinoxBackend : public IVinoxBackend {
public:
    explicit LocalVinoxBackend(const std::string& db_path = "vinox.db", const std::string& default_model_path = "mock");
    ~LocalVinoxBackend() override;

    bool is_connected() const override;
    std::string backend_name() const override { return "Local (C-ABI)"; }

    std::vector<ModelInfo> list_models() override;
    void scan_models(const std::string& directory_path) override;
    bool load_model(const std::string& model_id, const std::string& device) override;
    bool unload_model(const std::string& model_id) override;
    VinoxModelConfig get_model_config(const std::string& model_path) override;
    bool save_model_config(const std::string& model_path, const VinoxModelConfig& config) override;
    bool reset_model_config(const std::string& model_path) override;
    StorageHardwareInfo query_storage_hardware(const std::string& path = "") override;
    uint64_t get_cache_size(const std::string& cache_dir = "") override;
    bool clear_cache(const std::string& cache_dir = "") override;

    bool load_embedding_model(const std::string& model_path, const std::string& device = "CPU") override;
    bool unload_embedding_model() override;
    std::string current_embedding_device() const override { return current_embedding_device_; }

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
    bool ingest_document(const std::string& title, const std::string& content) override;

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
    std::string db_path_;
    std::string current_model_id_;
    std::string current_device_{"CPU"};
    std::vector<ModelInfo> available_models_;

    mutable std::mutex model_mutex_;
    vinox_model* model_{nullptr};

    mutable std::mutex embedding_mutex_;
    vinox_embedding_engine* embedding_engine_{nullptr};
    std::string current_embedding_device_{"CPU"};

    mutable std::mutex storage_mutex_;
    vinox_storage_engine* storage_{nullptr};
    std::vector<std::pair<std::string, std::string>> created_conversations_;

    vinox_tool_registry* tool_registry_{nullptr};
    vinox_mode_controller* mode_controller_{nullptr};

    std::mutex plan_mutex_;
    std::map<std::string, PlanData> plans_;

    std::mutex run_mutex_;
    std::map<std::string, AgentRunStatus> runs_;
    std::map<std::string, DiffArtifact> pending_diffs_;

    std::atomic<bool> cancel_requested_{false};
};

} // namespace vinox::gui
