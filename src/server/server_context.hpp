#pragma once

#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "vinox/vinox.h"
#include "vinox/openvino.h"
#include "vinox/storage.h"
#include "vinox/serving.h"
#include "vinox/tools.h"
#include "vinox/vinox_agent.h"
#include "vinox/embedding.h"
#include "vinox/plugins.h"

namespace vinox::server {

struct AgentRunState {
    std::string run_id;
    std::string plan_id;
    std::string plan_hash;
    std::string workspace_dir;
    vinox_plan_status status{VINOX_PLAN_STATUS_DRAFT};
    int completed_steps{0};
    std::vector<nlohmann::json> events;
    std::chrono::system_clock::time_point created_at;
};

struct ServerContext {
    // Configuration
    std::string host{"127.0.0.1"};
    int port{8080};
    std::string model_path;
    std::string embedding_model_path;
    std::string models_dir;
    std::string device{"AUTO"};
    std::string embedding_device{"CPU"};
    std::string prioritized_device{"NPU"};
    std::vector<vinox_device_info> detected_devices;
    bool has_npu{false};
    std::string db_path{"vinox.db"};
    std::string api_key;
    std::string plugins_dir{"plugins"};
    bool cors_enabled{false};
    std::string cors_origin{"*"};

    // Runtime handles & mutexes
    std::mutex model_mutex;
    vinox_model* model{nullptr};
    std::vector<vinox_tool_plugin*> loaded_plugins;
    vinox_model_protocol_contract protocol{};
    bool has_protocol{false};

    std::mutex embedding_mutex;
    vinox_embedding_engine* embedding_engine{nullptr};

    std::mutex storage_mutex;
    vinox_storage_engine* storage{nullptr};

    vinox_model_registry* registry{nullptr};
    vinox_tool_registry* tool_registry{nullptr};
    vinox_mode_controller* mode_controller{nullptr};

    // Model metadata & defaults
    std::string model_badge_info;
    std::string model_architecture;
    std::string model_quantization;
    uint64_t model_context_window{32768};
    float default_temperature{0.7f};
    float default_top_p{0.9f};
    float default_repetition_penalty{1.15f};
    float default_presence_penalty{0.0f};
    float default_frequency_penalty{0.0f};
    uint64_t default_max_tokens{512};
    bool has_custom_config{false};

    // Metrics
    std::atomic<uint64_t> request_count{0};
    std::atomic<uint64_t> prompt_tokens_total{0};
    std::atomic<uint64_t> completion_tokens_total{0};
    std::atomic<uint64_t> active_connections{0};
    std::chrono::system_clock::time_point start_time{std::chrono::system_clock::now()};

    // Plan & Agent state
    std::mutex plans_mutex;
    std::map<std::string, nlohmann::json> plans;

    std::mutex runs_mutex;
    std::map<std::string, AgentRunState> runs;

    bool is_ready() const {
        return (model != nullptr || !model_path.empty()) && (storage != nullptr);
    }
};

inline void detect_hardware_devices(ServerContext& ctx) {
    vinox_device_info devs[8];
    size_t count = 0;
    char prio[32] = {0};
    if (vinox_devices_query(devs, 8, &count, prio, sizeof(prio)) == VINOX_STATUS_OK) {
        ctx.detected_devices.clear();
        ctx.prioritized_device = prio;
        for (size_t i = 0; i < count; ++i) {
            ctx.detected_devices.push_back(devs[i]);
            if (std::string(devs[i].device_id).find("NPU") != std::string::npos) {
                ctx.has_npu = true;
            }
        }
        if (ctx.device.empty() || ctx.device == "AUTO") {
            // Strict priority: NPU has highest priority!
            ctx.device = ctx.prioritized_device;
        }
    } else {
        if (ctx.device.empty() || ctx.device == "AUTO") {
            ctx.device = "CPU";
        }
    }
}

inline void inspect_model_directory(const std::string& path, ServerContext& ctx) {
    if (path.empty()) return;
    namespace fs = std::filesystem;
    fs::path p(path);
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) return;

    std::string dir_name = p.filename().string();
    std::string lower_name = dir_name;
    for (char& c : lower_name) c = static_cast<char>(std::tolower(c));

    std::string params;
    if (lower_name.find("0.5b") != std::string::npos) params = "0.5B";
    else if (lower_name.find("1.5b") != std::string::npos) params = "1.5B";
    else if (lower_name.find("1b") != std::string::npos) params = "1B";
    else if (lower_name.find("3b") != std::string::npos) params = "3B";
    else if (lower_name.find("4b") != std::string::npos) params = "4B";
    else if (lower_name.find("7b") != std::string::npos) params = "7B";
    else if (lower_name.find("8b") != std::string::npos) params = "8B";
    else if (lower_name.find("14b") != std::string::npos) params = "14B";
    else if (lower_name.find("32b") != std::string::npos) params = "32B";

    if (lower_name.find("int4") != std::string::npos || lower_name.find("int-4") != std::string::npos) ctx.model_quantization = "INT4";
    else if (lower_name.find("int8") != std::string::npos) ctx.model_quantization = "INT8";
    else if (lower_name.find("fp16") != std::string::npos) ctx.model_quantization = "FP16";
    else ctx.model_quantization = "FP16";

    // 1. Check vinox_config.json
    fs::path vinox_cfg = p / "vinox_config.json";
    if (fs::exists(vinox_cfg, ec)) {
        try {
            std::ifstream vf(vinox_cfg);
            nlohmann::json vj;
            vf >> vj;
            if (vj.contains("temperature") && vj["temperature"].is_number()) ctx.default_temperature = vj["temperature"].get<float>();
            if (vj.contains("top_p") && vj["top_p"].is_number()) ctx.default_top_p = vj["top_p"].get<float>();
            if (vj.contains("repetition_penalty") && vj["repetition_penalty"].is_number()) ctx.default_repetition_penalty = vj["repetition_penalty"].get<float>();
            if (vj.contains("presence_penalty") && vj["presence_penalty"].is_number()) ctx.default_presence_penalty = vj["presence_penalty"].get<float>();
            if (vj.contains("frequency_penalty") && vj["frequency_penalty"].is_number()) ctx.default_frequency_penalty = vj["frequency_penalty"].get<float>();
            if (vj.contains("max_tokens") && vj["max_tokens"].is_number_integer()) ctx.default_max_tokens = vj["max_tokens"].get<uint64_t>();
            if (vj.contains("preferred_device") && vj["preferred_device"].is_string() && ctx.device == "CPU") {
                ctx.device = vj["preferred_device"].get<std::string>();
            }
            ctx.has_custom_config = true;
        } catch (...) {}
    }

    // 2. Check generation_config.json
    fs::path gen_cfg = p / "generation_config.json";
    if (fs::exists(gen_cfg, ec) && !ctx.has_custom_config) {
        try {
            std::ifstream gf(gen_cfg);
            nlohmann::json gj;
            gf >> gj;
            if (gj.contains("temperature") && gj["temperature"].is_number()) ctx.default_temperature = gj["temperature"].get<float>();
            if (gj.contains("top_p") && gj["top_p"].is_number()) ctx.default_top_p = gj["top_p"].get<float>();
            if (gj.contains("repetition_penalty") && gj["repetition_penalty"].is_number()) ctx.default_repetition_penalty = gj["repetition_penalty"].get<float>();
        } catch (...) {}
    }

    // 3. Check config.json
    fs::path cfg_file = p / "config.json";
    ctx.model_architecture = "Transformer";
    if (fs::exists(cfg_file, ec)) {
        try {
            std::ifstream cf(cfg_file);
            nlohmann::json cj;
            cf >> cj;
            if (cj.contains("model_type") && cj["model_type"].is_string()) {
                ctx.model_architecture = cj["model_type"].get<std::string>();
                if (!ctx.model_architecture.empty()) ctx.model_architecture[0] = static_cast<char>(std::toupper(ctx.model_architecture[0]));
            } else if (cj.contains("architectures") && cj["architectures"].is_array() && !cj["architectures"].empty()) {
                ctx.model_architecture = cj["architectures"][0].get<std::string>();
            }
            if (cj.contains("max_position_embeddings") && cj["max_position_embeddings"].is_number_integer()) {
                ctx.model_context_window = cj["max_position_embeddings"].get<uint64_t>();
            }
            if (params.empty() && cj.contains("hidden_size") && cj["hidden_size"].is_number_integer()) {
                int hs = cj["hidden_size"].get<int>();
                if (hs <= 1024) params = "0.5B";
                else if (hs <= 1536) params = "1.5B";
                else if (hs <= 2048) params = "2B-3B";
                else if (hs <= 4096) params = "7B-8B";
                else params = "14B+";
            }
        } catch (...) {}
    }

    std::string ctx_str;
    if (ctx.model_context_window >= 1000) {
        ctx_str = std::to_string(ctx.model_context_window / 1024) + "k Kontext";
    } else {
        ctx_str = std::to_string(ctx.model_context_window) + " Kontext";
    }

    std::string accel = ctx.model_quantization;
    if (ctx.device == "NPU" || lower_name.find("npu") != std::string::npos || ctx.model_quantization == "INT4" || ctx.model_quantization == "INT8" || ctx.model_quantization == "FP16") {
        accel += " NPU";
    }

    std::ostringstream ss;
    ss << ctx.model_architecture;
    if (!params.empty()) ss << " (" << params << ")";
    ss << " • " << ctx_str << " • " << accel << " • Default Temp: " << std::fixed << std::setprecision(1) << ctx.default_temperature;
    ctx.model_badge_info = ss.str();
}

} // namespace vinox::server
