#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vinox::gui {

enum class RiskLevel {
    Low,
    Medium,
    High
};

struct ChatMessage {
    std::string id;
    std::string role; // "user", "assistant", "system"
    std::string content;
    std::string reasoning_content;
    uint64_t token_count{0};
    uint64_t timestamp_ms{0};
};

struct ConversationInfo {
    std::string id;
    std::string title;
    uint64_t created_at_ms{0};
    uint64_t updated_at_ms{0};
    int64_t message_count{0};
};

struct ModelInfo {
    std::string id;
    std::string name;
    std::string device; // "CPU", "GPU", "NPU"
    bool is_loaded{false};
    uint64_t context_window{32768};
    std::string badge_info;
    std::string architecture;
    std::string quantization;
    double default_temperature{0.7};
};

struct StorageHardwareInfo {
    bool is_nvme{false};
    bool is_ssd{false};
    std::string bus_type{"Standard"};
    std::string device_name;
    uint64_t total_bytes{0};
    uint64_t free_bytes{0};
};

struct VinoxModelConfig {
    double temperature{0.7};
    double top_p{0.9};
    double repetition_penalty{1.15};
    double presence_penalty{0.0};
    double frequency_penalty{0.0};
    int max_tokens{512};
    std::string preferred_device{"CPU"};
    bool enable_mmap{true};
    bool enable_cache{true};
    std::string cache_dir{"C:\\ai\\openvino\\cache\\blobs"};
    bool is_custom{false}; // true if loaded from vinox_config.json
};

struct PlanStep {
    uint32_t index{0};
    std::string description;
    std::string capability;
    RiskLevel risk{RiskLevel::Low};
};

struct PlanData {
    std::string plan_id;
    std::string plan_hash;
    std::string task;
    std::vector<PlanStep> steps;
    bool is_approved{false};
};

struct AgentEvent {
    uint64_t sequence_id{0};
    std::string event_type; // "step_start", "action_tool", "observation", "checkpoint", "error", "complete"
    std::string tool_name;
    std::string tool_args;
    std::string tool_result;
    std::string message;
    uint64_t timestamp_ms{0};
};

struct AgentRunStatus {
    std::string run_id;
    std::string state; // "idle", "running", "completed", "cancelled", "failed"
    uint32_t steps_completed{0};
    uint32_t total_steps{0};
    uint64_t tokens_spent{0};
    uint64_t token_budget{100000};
    std::string last_error;
};

struct SearchMatch {
    std::string id;
    std::string title;
    std::string snippet;
    float score{0.0f};
    std::string match_type; // "fts", "vector", "hybrid"
};

struct EntityRelation {
    std::string source_id;
    std::string target_id;
    std::string relation_type;
    int depth{1};
};

struct DiffHunk {
    uint32_t id{0};
    std::string file_path;
    uint32_t old_start{0};
    uint32_t old_lines{0};
    uint32_t new_start{0};
    uint32_t new_lines{0};
    std::string diff_text;
    bool is_selected{true};
};

struct DiffArtifact {
    std::string base_snapshot_hash;
    std::vector<DiffHunk> hunks;
    uint32_t total_additions{0};
    uint32_t total_deletions{0};
};

using StreamTokenCallback = std::function<void(const std::string& token, bool is_reasoning)>;
using AgentEventCallback = std::function<void(const AgentEvent& event)>;

class IVinoxBackend {
public:
    virtual ~IVinoxBackend() = default;

    // Connection & Lifecycle
    virtual bool is_connected() const = 0;
    virtual std::string backend_name() const = 0; // "Local (C-ABI)" or "Remote (HTTP/SSE)"

    // Model Management
    virtual std::vector<ModelInfo> list_models() = 0;
    virtual void scan_models(const std::string& directory_path) { (void)directory_path; }
    virtual bool load_model(const std::string& model_id, const std::string& device) = 0;
    virtual bool unload_model(const std::string& model_id) = 0;
    virtual VinoxModelConfig get_model_config(const std::string& model_path) {
        (void)model_path;
        return VinoxModelConfig{};
    }
    virtual bool save_model_config(const std::string& model_path, const VinoxModelConfig& config) {
        (void)model_path; (void)config;
        return false;
    }
    virtual bool reset_model_config(const std::string& model_path) {
        (void)model_path;
        return false;
    }
    virtual StorageHardwareInfo query_storage_hardware(const std::string& path = "") {
        (void)path;
        return StorageHardwareInfo{};
    }
    virtual uint64_t get_cache_size(const std::string& cache_dir = "") {
        (void)cache_dir;
        return 0;
    }
    virtual bool clear_cache(const std::string& cache_dir = "") {
        (void)cache_dir;
        return false;
    }

    // Decoupled Embedding Management
    virtual bool load_embedding_model(const std::string& model_path, const std::string& device = "CPU") {
        (void)model_path; (void)device;
        return false;
    }
    virtual bool unload_embedding_model() { return false; }
    virtual std::string current_embedding_device() const { return "CPU"; }

    // Chat Inference
    virtual bool generate_chat_stream(
        const std::vector<ChatMessage>& history,
        float temperature,
        float top_p,
        uint64_t max_tokens,
        StreamTokenCallback on_token,
        std::string& out_error,
        const std::string& conversation_id = ""
    ) = 0;
    virtual void cancel_generation() = 0;

    // Storage, Search & Relations
    virtual std::vector<std::pair<std::string, std::string>> list_conversations() = 0; // id, title
    virtual std::vector<ConversationInfo> list_conversations_detailed() {
        std::vector<ConversationInfo> detailed;
        for (const auto& p : list_conversations()) {
            detailed.push_back({p.first, p.second, 0, 0, 0});
        }
        return detailed;
    }
    virtual std::string create_conversation(const std::string& title) = 0;
    virtual std::vector<ChatMessage> get_conversation_messages(const std::string& conversation_id) { (void)conversation_id; return {}; }
    virtual bool delete_conversation(const std::string& conversation_id) { (void)conversation_id; return false; }
    virtual std::vector<SearchMatch> search_hybrid(const std::string& query, float alpha, uint32_t limit) = 0;
    virtual std::vector<EntityRelation> get_relations(const std::string& source_id) = 0;
    virtual bool ingest_document(const std::string& title, const std::string& content) { (void)title; (void)content; return false; }

    // Plan & Governance
    virtual PlanData create_plan(const std::string& task) = 0;
    virtual bool approve_plan(const std::string& plan_id, const std::string& expected_plan_hash) = 0;

    // Agent Runs & Sandboxing
    virtual std::string start_agent_run(
        const std::string& plan_id,
        const std::string& plan_hash,
        const std::string& workspace_dir,
        AgentEventCallback on_event
    ) = 0;
    virtual AgentRunStatus get_agent_run_status(const std::string& run_id) = 0;
    virtual bool cancel_agent_run(const std::string& run_id) = 0;

    // Diff & Snapshot
    virtual DiffArtifact get_pending_diff(const std::string& run_id) = 0;
    virtual bool apply_diff(const std::string& run_id, const std::string& base_snapshot_hash, const std::vector<uint32_t>& selected_hunk_ids) = 0;
};

} // namespace vinox::gui
