#include "local_backend.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace vinox::gui {

static std::string calculate_hash(const std::string& input) {
    uint64_t h = 14695981039346656037ULL;
    for (char c : input) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ULL;
    }
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << h;
    return ss.str();
}

LocalVinoxBackend::LocalVinoxBackend(const std::string& db_path, const std::string& default_model_path)
    : db_path_(db_path), current_model_id_(default_model_path) {
    vinox_storage_engine_open(db_path_.c_str(), &storage_);
    vinox_tool_registry_create(&tool_registry_);
    mode_controller_ = vinox_mode_controller_create();

    load_model(default_model_path, "CPU");
}

LocalVinoxBackend::~LocalVinoxBackend() {
    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        if (model_) {
            vinox_model_destroy(model_);
            model_ = nullptr;
        }
    }
    {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        if (storage_) {
            vinox_storage_engine_close(storage_);
            storage_ = nullptr;
        }
    }
    {
        std::lock_guard<std::mutex> lock(embedding_mutex_);
        if (embedding_engine_) {
            vinox_embedding_engine_destroy(embedding_engine_);
            embedding_engine_ = nullptr;
        }
    }
    if (tool_registry_) {
        vinox_tool_registry_destroy(tool_registry_);
        tool_registry_ = nullptr;
    }
    if (mode_controller_) {
        vinox_mode_controller_destroy(mode_controller_);
        mode_controller_ = nullptr;
    }
}

bool LocalVinoxBackend::is_connected() const {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    return storage_ != nullptr;
}

std::vector<ModelInfo> LocalVinoxBackend::list_models() {
    std::lock_guard<std::mutex> lock(model_mutex_);
    if (!available_models_.empty()) {
        for (auto& m : available_models_) {
            m.is_loaded = (m.id == current_model_id_ && model_ != nullptr);
        }
        return available_models_;
    }
    std::vector<ModelInfo> result;
    result.push_back({
        current_model_id_.empty() ? "mock" : current_model_id_,
        current_model_id_.empty() ? "Mock Model" : current_model_id_,
        current_device_,
        model_ != nullptr,
        32768
    });
    return result;
}

void LocalVinoxBackend::scan_models(const std::string& directory_path) {
    std::lock_guard<std::mutex> lock(model_mutex_);
    available_models_.clear();
    std::error_code ec;
    if (!std::filesystem::exists(directory_path, ec) || !std::filesystem::is_directory(directory_path, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory_path, ec)) {
        if (entry.is_directory(ec)) {
            std::string sub_path = entry.path().string();
            std::string dir_name = entry.path().filename().string();
            if (dir_name.empty() || dir_name[0] == '.') continue;

            bool has_model = false;
            for (const auto& sub_entry : std::filesystem::directory_iterator(entry.path(), ec)) {
                std::string fname = sub_entry.path().filename().string();
                if (fname == "openvino_model.xml" || fname == "model.xml" || fname == "config.json") {
                    has_model = true;
                    break;
                }
            }

            if (has_model) {
                ModelInfo info;
                info.id = sub_path;
                info.name = dir_name;
                info.device = current_device_;
                info.is_loaded = (sub_path == current_model_id_ && model_ != nullptr);
                info.context_window = 32768;
                info.architecture = "Transformer";
                info.quantization = "FP16";
                info.default_temperature = 0.7;

                std::string params = "";
                std::string lower_name = dir_name;
                for (char& c : lower_name) c = static_cast<char>(std::tolower(c));

                if (lower_name.find("0.5b") != std::string::npos) params = "0.5B";
                else if (lower_name.find("1.5b") != std::string::npos) params = "1.5B";
                else if (lower_name.find("1b") != std::string::npos) params = "1B";
                else if (lower_name.find("3b") != std::string::npos) params = "3B";
                else if (lower_name.find("4b") != std::string::npos) params = "4B";
                else if (lower_name.find("7b") != std::string::npos) params = "7B";
                else if (lower_name.find("8b") != std::string::npos) params = "8B";
                else if (lower_name.find("14b") != std::string::npos) params = "14B";
                else if (lower_name.find("32b") != std::string::npos) params = "32B";

                if (lower_name.find("int4") != std::string::npos || lower_name.find("int-4") != std::string::npos) info.quantization = "INT4";
                else if (lower_name.find("int8") != std::string::npos) info.quantization = "INT8";
                else if (lower_name.find("fp16") != std::string::npos) info.quantization = "FP16";

                // Parse config.json
                std::filesystem::path cfg_file = entry.path() / "config.json";
                if (std::filesystem::exists(cfg_file, ec)) {
                    try {
                        std::ifstream cf(cfg_file);
                        nlohmann::json cj;
                        cf >> cj;
                        if (cj.contains("model_type") && cj["model_type"].is_string()) {
                            info.architecture = cj["model_type"].get<std::string>();
                            if (!info.architecture.empty()) info.architecture[0] = static_cast<char>(std::toupper(info.architecture[0]));
                        } else if (cj.contains("architectures") && cj["architectures"].is_array() && !cj["architectures"].empty()) {
                            info.architecture = cj["architectures"][0].get<std::string>();
                        }
                        if (cj.contains("max_position_embeddings") && cj["max_position_embeddings"].is_number_integer()) {
                            info.context_window = cj["max_position_embeddings"].get<uint64_t>();
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

                // Parse generation_config.json
                std::filesystem::path gen_file = entry.path() / "generation_config.json";
                if (std::filesystem::exists(gen_file, ec)) {
                    try {
                        std::ifstream gf(gen_file);
                        nlohmann::json gj;
                        gf >> gj;
                        if (gj.contains("temperature") && gj["temperature"].is_number()) {
                            info.default_temperature = gj["temperature"].get<double>();
                        }
                    } catch (...) {}
                }

                std::string ctx_str;
                if (info.context_window >= 1000) {
                    ctx_str = std::to_string(info.context_window / 1024) + "k Kontext";
                } else {
                    ctx_str = std::to_string(info.context_window) + " Kontext";
                }

                std::string accel = info.quantization;
                if (info.device == "NPU" || lower_name.find("npu") != std::string::npos || info.quantization == "INT4" || info.quantization == "INT8" || info.quantization == "FP16") {
                    accel += " NPU";
                }

                std::ostringstream ss;
                ss << info.architecture;
                if (!params.empty()) {
                    ss << " (" << params << ")";
                }
                ss << " • " << ctx_str << " • " << accel << " • Default Temp: " << std::fixed << std::setprecision(1) << info.default_temperature;
                info.badge_info = ss.str();

                available_models_.push_back(info);
            }
        }
    }
}

bool LocalVinoxBackend::load_model(const std::string& model_id, const std::string& device) {
    std::lock_guard<std::mutex> lock(model_mutex_);
    if (model_) {
        vinox_model_destroy(model_);
        model_ = nullptr;
    }

    VinoxModelConfig cfg = get_model_config(model_id);

    vinox_model_options m_opts{};
    m_opts.struct_size = sizeof(m_opts);
    m_opts.model_path = model_id.c_str();
    m_opts.device = device.c_str();
    m_opts.enable_mmap = cfg.enable_mmap ? 1 : 0;
    m_opts.enable_cache = cfg.enable_cache ? 1 : 0;
    std::string c_dir = cfg.cache_dir.empty() ? "C:\\ai\\openvino\\cache\\blobs" : cfg.cache_dir;
    m_opts.cache_dir = c_dir.c_str();

    vinox_status st = vinox_model_load(&m_opts, &model_);
    if (st == VINOX_STATUS_OK) {
        current_model_id_ = model_id;
        current_device_ = device;
        for (auto& m : available_models_) {
            m.is_loaded = (m.id == current_model_id_);
        }
        return true;
    }
    return false;
}

bool LocalVinoxBackend::unload_model(const std::string&) {
    std::lock_guard<std::mutex> lock(model_mutex_);
    if (model_) {
        vinox_model_destroy(model_);
        model_ = nullptr;
        return true;
    }
    return false;
}

VinoxModelConfig LocalVinoxBackend::get_model_config(const std::string& model_path) {
    VinoxModelConfig config{};
    if (model_path.empty()) return config;

    namespace fs = std::filesystem;
    fs::path p(model_path);
    if (!fs::exists(p) || !fs::is_directory(p)) return config;

    // Priority 1: Check for custom vinox_config.json in model folder
    fs::path vinox_cfg_path = p / "vinox_config.json";
    if (fs::exists(vinox_cfg_path)) {
        try {
            std::ifstream f(vinox_cfg_path);
            nlohmann::json j;
            f >> j;
            if (j.contains("temperature") && j["temperature"].is_number()) config.temperature = j["temperature"].get<double>();
            if (j.contains("top_p") && j["top_p"].is_number()) config.top_p = j["top_p"].get<double>();
            if (j.contains("repetition_penalty") && j["repetition_penalty"].is_number()) config.repetition_penalty = j["repetition_penalty"].get<double>();
            if (j.contains("presence_penalty") && j["presence_penalty"].is_number()) config.presence_penalty = j["presence_penalty"].get<double>();
            if (j.contains("frequency_penalty") && j["frequency_penalty"].is_number()) config.frequency_penalty = j["frequency_penalty"].get<double>();
            if (j.contains("max_tokens") && j["max_tokens"].is_number_integer()) config.max_tokens = j["max_tokens"].get<int>();
            if (j.contains("preferred_device") && j["preferred_device"].is_string()) config.preferred_device = j["preferred_device"].get<std::string>();
            if (j.contains("enable_mmap") && j["enable_mmap"].is_boolean()) config.enable_mmap = j["enable_mmap"].get<bool>();
            if (j.contains("enable_cache") && j["enable_cache"].is_boolean()) config.enable_cache = j["enable_cache"].get<bool>();
            if (j.contains("cache_dir") && j["cache_dir"].is_string()) config.cache_dir = j["cache_dir"].get<std::string>();
            config.is_custom = true;
            return config;
        } catch (...) {
            // Fall through to defaults on parse error
        }
    }

    // Priority 2: Fallback to model's generation_config.json
    fs::path gen_cfg_path = p / "generation_config.json";
    if (fs::exists(gen_cfg_path)) {
        try {
            std::ifstream f(gen_cfg_path);
            nlohmann::json j;
            f >> j;
            if (j.contains("temperature") && j["temperature"].is_number()) config.temperature = j["temperature"].get<double>();
            if (j.contains("top_p") && j["top_p"].is_number()) config.top_p = j["top_p"].get<double>();
            if (j.contains("repetition_penalty") && j["repetition_penalty"].is_number()) config.repetition_penalty = j["repetition_penalty"].get<double>();
        } catch (...) {}
    }

    if (config.repetition_penalty <= 0.0) {
        config.repetition_penalty = 1.15;
    }
    config.is_custom = false;
    return config;
}

bool LocalVinoxBackend::save_model_config(const std::string& model_path, const VinoxModelConfig& config) {
    if (model_path.empty()) return false;
    namespace fs = std::filesystem;
    fs::path p(model_path);
    if (!fs::exists(p) || !fs::is_directory(p)) return false;

    fs::path vinox_cfg_path = p / "vinox_config.json";
    try {
        nlohmann::json j;
        j["vinox_version"] = "1.0";
        j["temperature"] = config.temperature;
        j["top_p"] = config.top_p;
        j["repetition_penalty"] = config.repetition_penalty;
        j["presence_penalty"] = config.presence_penalty;
        j["frequency_penalty"] = config.frequency_penalty;
        j["max_tokens"] = config.max_tokens;
        j["preferred_device"] = config.preferred_device.empty() ? "CPU" : config.preferred_device;
        j["enable_mmap"] = config.enable_mmap;
        j["enable_cache"] = config.enable_cache;
        j["cache_dir"] = config.cache_dir;

        std::ofstream f(vinox_cfg_path);
        f << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool LocalVinoxBackend::reset_model_config(const std::string& model_path) {
    if (model_path.empty()) return false;
    namespace fs = std::filesystem;
    fs::path p(model_path);
    fs::path vinox_cfg_path = p / "vinox_config.json";
    std::error_code ec;
    if (fs::exists(vinox_cfg_path, ec)) {
        return fs::remove(vinox_cfg_path, ec);
    }
    return true;
}

StorageHardwareInfo LocalVinoxBackend::query_storage_hardware(const std::string& path) {
    StorageHardwareInfo res{};
    vinox_storage_info info{};
    info.struct_size = sizeof(info);
    std::string p = path.empty() ? "C:\\" : path;
    if (vinox_storage_detect(p.c_str(), &info) == VINOX_STATUS_OK) {
        res.is_nvme = (info.is_nvme != 0);
        res.is_ssd = (info.is_ssd != 0);
        res.bus_type = info.bus_type_name;
        res.device_name = info.device_name;
        res.total_bytes = info.total_bytes;
        res.free_bytes = info.free_bytes;
    }
    return res;
}

uint64_t LocalVinoxBackend::get_cache_size(const std::string& cache_dir) {
    namespace fs = std::filesystem;
    std::string dir = cache_dir.empty() ? "C:\\ai\\openvino\\cache\\blobs" : cache_dir;
    uint64_t total = 0;
    std::error_code ec;
    if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec)) {
                total += entry.file_size(ec);
            }
        }
    }
    return total;
}

bool LocalVinoxBackend::clear_cache(const std::string& cache_dir) {
    namespace fs = std::filesystem;
    std::string dir = cache_dir.empty() ? "C:\\ai\\openvino\\cache\\blobs" : cache_dir;
    std::error_code ec;
    if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == ".blob") {
                fs::remove(entry.path(), ec);
            }
        }
        return true;
    }
    return false;
}

bool LocalVinoxBackend::generate_chat_stream(
    const std::vector<ChatMessage>& history,
    float temperature,
    float top_p,
    uint64_t max_tokens,
    StreamTokenCallback on_token,
    std::string& out_error,
    const std::string& conversation_id
) {
    cancel_requested_.store(false);
    std::lock_guard<std::mutex> lock(model_mutex_);
    if (!model_) {
        out_error = "No model is loaded";
        return false;
    }

    std::string prompt;
    for (const auto& msg : history) {
        prompt += msg.role + ": " + msg.content + "\n";
    }
    prompt += "assistant: ";

    std::string user_msg_id;
    if (!conversation_id.empty() && !history.empty()) {
        std::lock_guard<std::mutex> st_lock(storage_mutex_);
        if (storage_) {
            const auto& last_msg = history.back();
            if (last_msg.role == "user") {
                vinox_message_info u_msg{};
                u_msg.struct_size = sizeof(u_msg);
                u_msg.conversation_id = conversation_id.c_str();
                u_msg.role = "user";
                u_msg.content = last_msg.content.c_str();
                vinox_message_info u_out{};
                u_out.struct_size = sizeof(u_out);
                if (vinox_storage_add_message(storage_, &u_msg, &u_out) == VINOX_STATUS_OK && u_out.id) {
                    user_msg_id = u_out.id;
                }
            }
        }
    }

    vinox_generation_options gen_opts{};
    gen_opts.struct_size = sizeof(gen_opts);
    gen_opts.prompt = prompt.c_str();
    gen_opts.max_new_tokens = max_tokens;
    gen_opts.temperature = temperature;
    gen_opts.top_p = top_p;
    gen_opts.top_k = 50;
    gen_opts.repetition_penalty = 1.15f;
    gen_opts.presence_penalty = 0.1f;
    gen_opts.frequency_penalty = 0.1f;
    gen_opts.reasoning_mode = VINOX_REASONING_NONE;
    gen_opts.reasoning_can_disable = 1;

    std::string accumulated_asst;
    struct StreamContext {
        StreamTokenCallback& cb;
        std::atomic<bool>& cancel;
        vinox_model* m;
        std::string& asst_out;
    } s_ctx{on_token, cancel_requested_, model_, accumulated_asst};

    auto stream_cb = [](vinox_stream_channel channel, const char* text, size_t text_size, void* user_data) -> int {
        auto* c = static_cast<StreamContext*>(user_data);
        if (c->cancel.load()) {
            vinox_model_cancel(c->m);
            return 1;
        }
        std::string token(text, text_size);
        bool is_reasoning = (channel == VINOX_STREAM_CHANNEL_REASONING);
        if (!is_reasoning) {
            c->asst_out.append(token);
        }
        c->cb(token, is_reasoning);
        return 0;
    };

    vinox_status st = vinox_model_generate_stream(model_, &gen_opts, stream_cb, &s_ctx);
    if (st != VINOX_STATUS_OK && st != VINOX_STATUS_CANCELLED) {
        out_error = vinox_openvino_last_error();
        return false;
    }

    if (!conversation_id.empty() && !accumulated_asst.empty()) {
        std::lock_guard<std::mutex> st_lock(storage_mutex_);
        if (storage_) {
            vinox_message_info a_msg{};
            a_msg.struct_size = sizeof(a_msg);
            a_msg.conversation_id = conversation_id.c_str();
            if (!user_msg_id.empty()) a_msg.parent_id = user_msg_id.c_str();
            a_msg.role = "assistant";
            a_msg.content = accumulated_asst.c_str();
            vinox_message_info a_out{};
            a_out.struct_size = sizeof(a_out);
            vinox_storage_add_message(storage_, &a_msg, &a_out);
        }
    }

    return true;
}

void LocalVinoxBackend::cancel_generation() {
    cancel_requested_.store(true);
    std::lock_guard<std::mutex> lock(model_mutex_);
    if (model_) {
        vinox_model_cancel(model_);
    }
}

std::vector<std::pair<std::string, std::string>> LocalVinoxBackend::list_conversations() {
    std::vector<std::pair<std::string, std::string>> result;
    std::lock_guard<std::mutex> lock(storage_mutex_);
    if (!storage_) return created_conversations_;

    char json_buf[65536] = {0};
    if (vinox_storage_list_conversations_json(storage_, json_buf, sizeof(json_buf)) == VINOX_STATUS_OK) {
        try {
            auto j = nlohmann::json::parse(json_buf);
            if (j.is_array()) {
                for (const auto& item : j) {
                    result.emplace_back(item.value("id", ""), item.value("title", "Untitled"));
                }
                return result;
            }
        } catch (...) {}
    }
    return created_conversations_;
}

std::vector<ConversationInfo> LocalVinoxBackend::list_conversations_detailed() {
    std::vector<ConversationInfo> result;
    std::lock_guard<std::mutex> lock(storage_mutex_);
    if (!storage_) return result;

    char json_buf[65536] = {0};
    if (vinox_storage_list_conversations_json(storage_, json_buf, sizeof(json_buf)) == VINOX_STATUS_OK) {
        try {
            auto j = nlohmann::json::parse(json_buf);
            if (j.is_array()) {
                for (const auto& item : j) {
                    ConversationInfo info;
                    info.id = item.value("id", "");
                    info.title = item.value("title", "Untitled");
                    info.created_at_ms = item.value("created_at_ms", 0ULL);
                    info.updated_at_ms = item.value("updated_at_ms", 0ULL);
                    info.message_count = item.value("message_count", 0LL);
                    result.push_back(info);
                }
            }
        } catch (...) {}
    }
    return result;
}

std::vector<ChatMessage> LocalVinoxBackend::get_conversation_messages(const std::string& conversation_id) {
    std::vector<ChatMessage> result;
    if (conversation_id.empty()) return result;
    std::lock_guard<std::mutex> lock(storage_mutex_);
    if (!storage_) return result;

    char json_buf[65536] = {0};
    if (vinox_storage_get_conversation_messages_json(storage_, conversation_id.c_str(), json_buf, sizeof(json_buf)) == VINOX_STATUS_OK) {
        try {
            auto j = nlohmann::json::parse(json_buf);
            if (j.is_array()) {
                for (const auto& item : j) {
                    ChatMessage msg;
                    msg.id = item.value("id", "");
                    msg.role = item.value("role", "user");
                    msg.content = item.value("content", "");
                    msg.reasoning_content = item.value("reasoning_content", "");
                    msg.timestamp_ms = item.value("created_at_ms", 0ULL);
                    result.push_back(msg);
                }
            }
        } catch (...) {}
    }
    return result;
}

bool LocalVinoxBackend::delete_conversation(const std::string& conversation_id) {
    if (conversation_id.empty()) return false;
    std::lock_guard<std::mutex> lock(storage_mutex_);
    if (!storage_) return false;
    return vinox_storage_delete_conversation(storage_, conversation_id.c_str()) == VINOX_STATUS_OK;
}

std::string LocalVinoxBackend::create_conversation(const std::string& title) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    if (!storage_) return "";

    vinox_conversation_info info{};
    info.struct_size = sizeof(info);
    if (vinox_storage_create_conversation(storage_, title.c_str(), &info) == VINOX_STATUS_OK) {
        std::string id = info.id ? info.id : "";
        std::string t = info.title ? info.title : title;
        created_conversations_.emplace_back(id, t);
        return id;
    }
    return "";
}

bool LocalVinoxBackend::load_embedding_model(const std::string& model_path, const std::string& device) {
    std::lock_guard<std::mutex> lock(embedding_mutex_);
    if (embedding_engine_) {
        vinox_embedding_engine_destroy(embedding_engine_);
        embedding_engine_ = nullptr;
    }

    vinox_embedding_options emb_opts{};
    emb_opts.struct_size = sizeof(emb_opts);
    emb_opts.model_path = model_path.c_str();
    emb_opts.device = device.empty() ? "CPU" : device.c_str();
    emb_opts.pooling_mode = VINOX_EMBEDDING_POOLING_AUTO;
    emb_opts.normalization = VINOX_EMBEDDING_NORM_AUTO;
    emb_opts.enable_mmap = 1;
    emb_opts.enable_cache = 1;

    vinox_status st = vinox_embedding_engine_create(&emb_opts, &embedding_engine_);
    if (st == VINOX_STATUS_OK) {
        current_embedding_device_ = device.empty() ? "CPU" : device;
        return true;
    }
    return false;
}

bool LocalVinoxBackend::unload_embedding_model() {
    std::lock_guard<std::mutex> lock(embedding_mutex_);
    if (embedding_engine_) {
        vinox_embedding_engine_destroy(embedding_engine_);
        embedding_engine_ = nullptr;
        return true;
    }
    return false;
}

std::vector<SearchMatch> LocalVinoxBackend::search_hybrid(const std::string& query, float alpha, uint32_t limit) {
    std::vector<SearchMatch> result;
    std::lock_guard<std::mutex> lock(storage_mutex_);
    if (!storage_ || limit == 0) return result;

    std::vector<float> query_vec;
    {
        std::lock_guard<std::mutex> emb_lock(embedding_mutex_);
        if (embedding_engine_) {
            size_t dim = 0;
            vinox_embedding_get_dim(embedding_engine_, &dim);
            if (dim > 0) {
                query_vec.resize(dim);
                size_t actual_dim = 0;
                vinox_embedding_generate(embedding_engine_, query.c_str(), query_vec.data(), query_vec.size(), &actual_dim);
            }
        }
    }

    std::vector<vinox_search_result> raw_results(limit);
    for (auto& r : raw_results) {
        r.struct_size = sizeof(r);
    }
    size_t count = 0;
    if (vinox_storage_search_hybrid(storage_, query_vec.empty() ? nullptr : query_vec.data(), query_vec.size(), query.c_str(), alpha, limit, raw_results.data(), &count) == VINOX_STATUS_OK) {
        for (size_t i = 0; i < count; ++i) {
            result.push_back({
                raw_results[i].message_id ? raw_results[i].message_id : "",
                "Message Match",
                query,
                raw_results[i].hybrid_score,
                query_vec.empty() ? "fts" : "hybrid"
            });
        }
    }
    return result;
}

std::vector<EntityRelation> LocalVinoxBackend::get_relations(const std::string& source_id) {
    std::vector<EntityRelation> result;
    std::lock_guard<std::mutex> lock(storage_mutex_);
    if (!storage_ || source_id.empty()) return result;

    char json_buf[8192] = {0};
    if (vinox_storage_relations_query_cte(storage_, source_id.c_str(), json_buf, sizeof(json_buf)) == VINOX_STATUS_OK) {
        try {
            auto j = nlohmann::json::parse(json_buf, nullptr, false);
            if (j.is_array()) {
                for (const auto& item : j) {
                    result.push_back({
                        item.value("source_id", ""),
                        item.value("target_id", ""),
                        item.value("type", ""),
                        item.value("depth", 1)
                    });
                }
            }
        } catch (...) {}
    }
    return result;
}

bool LocalVinoxBackend::ingest_document(const std::string& title, const std::string& content) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    if (!storage_ || title.empty() || content.empty()) return false;

    char doc_id_buf[64] = {0};
    vinox_status st = vinox_storage_document_ingest(storage_, title.c_str(), content.c_str(), doc_id_buf, sizeof(doc_id_buf));
    if (st != VINOX_STATUS_OK) return false;

    {
        std::lock_guard<std::mutex> emb_lock(embedding_mutex_);
        if (embedding_engine_) {
            size_t dim = 0;
            vinox_embedding_get_dim(embedding_engine_, &dim);
            if (dim > 0) {
                std::vector<float> vec(dim);
                size_t actual_dim = 0;
                std::string full_text = title + "\n" + content;
                if (vinox_embedding_generate(embedding_engine_, full_text.c_str(), vec.data(), vec.size(), &actual_dim) == VINOX_STATUS_OK) {
                    vinox_storage_store_chunk_embedding(storage_, doc_id_buf, vec.data(), vec.size());
                }
            }
        }
    }
    return true;
}

PlanData LocalVinoxBackend::create_plan(const std::string& task) {
    std::lock_guard<std::mutex> lock(plan_mutex_);

    std::string plan_id = "plan_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::string canonical_spec = "task:" + task + ";steps:inspect,modify,verify";
    std::string plan_hash = calculate_hash(canonical_spec);

    PlanData plan;
    plan.plan_id = plan_id;
    plan.plan_hash = plan_hash;
    plan.task = task;
    plan.is_approved = false;

    plan.steps = {
        {1, "Inspect workspace and environment targets", "fs:read", RiskLevel::Low},
        {2, "Modify target sources with bounded edits", "fs:write", RiskLevel::Medium},
        {3, "Verify changes with regression tests", "exec:test", RiskLevel::Low}
    };

    plans_[plan_id] = plan;
    return plan;
}

bool LocalVinoxBackend::approve_plan(const std::string& plan_id, const std::string& expected_plan_hash) {
    std::lock_guard<std::mutex> lock(plan_mutex_);
    auto it = plans_.find(plan_id);
    if (it == plans_.end()) return false;

    // Fail-Closed Hash-Bound Approval Guarantee
    if (it->second.plan_hash != expected_plan_hash) {
        return false;
    }

    it->second.is_approved = true;
    return true;
}

std::string LocalVinoxBackend::start_agent_run(
    const std::string& plan_id,
    const std::string& plan_hash,
    const std::string& workspace_dir,
    AgentEventCallback on_event
) {
    {
        std::lock_guard<std::mutex> lock(plan_mutex_);
        auto it = plans_.find(plan_id);
        if (it == plans_.end() || !it->second.is_approved || it->second.plan_hash != plan_hash) {
            return ""; // Rejected fail-closed
        }
    }

    std::string run_id = "run_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    {
        std::lock_guard<std::mutex> lock(run_mutex_);
        runs_[run_id] = {run_id, "running", 0, 3, 0, 100000, ""};

        // Prepare simulated pending diff artifact
        DiffArtifact artifact;
        artifact.base_snapshot_hash = "snap_root_001";
        artifact.total_additions = 12;
        artifact.total_deletions = 2;
        artifact.hunks.push_back({
            1,
            workspace_dir + "/main.cpp",
            10, 2, 10, 4,
            "@@ -10,2 +10,4 @@\n-    old_init();\n+    vinox_init();\n+    vinox_start();",
            true
        });
        pending_diffs_[run_id] = artifact;
    }

    // Emit initial sequenced events
    if (on_event) {
        on_event({1, "step_start", "", "", "", "Starting Step 1: Inspect environment", 100});
        on_event({2, "action_tool", "fs:read", "{\"path\":\"main.cpp\"}", "", "Inspecting main.cpp", 200});
        on_event({3, "observation", "fs:read", "", "{\"status\":\"ok\"}", "main.cpp loaded", 300});
    }

    return run_id;
}

AgentRunStatus LocalVinoxBackend::get_agent_run_status(const std::string& run_id) {
    std::lock_guard<std::mutex> lock(run_mutex_);
    auto it = runs_.find(run_id);
    if (it != runs_.end()) {
        return it->second;
    }
    return {run_id, "unknown", 0, 0, 0, 0, "Run not found"};
}

bool LocalVinoxBackend::cancel_agent_run(const std::string& run_id) {
    std::lock_guard<std::mutex> lock(run_mutex_);
    auto it = runs_.find(run_id);
    if (it != runs_.end()) {
        it->second.state = "cancelled";
        return true;
    }
    return false;
}

DiffArtifact LocalVinoxBackend::get_pending_diff(const std::string& run_id) {
    std::lock_guard<std::mutex> lock(run_mutex_);
    auto it = pending_diffs_.find(run_id);
    if (it != pending_diffs_.end()) {
        return it->second;
    }
    return {};
}

bool LocalVinoxBackend::apply_diff(
    const std::string& run_id,
    const std::string& base_snapshot_hash,
    const std::vector<uint32_t>& selected_hunk_ids
) {
    std::lock_guard<std::mutex> lock(run_mutex_);
    auto it = pending_diffs_.find(run_id);
    if (it == pending_diffs_.end()) return false;

    // Fail-Closed Snapshot Invariant
    if (it->second.base_snapshot_hash != base_snapshot_hash) {
        return false;
    }

    if (selected_hunk_ids.empty()) {
        return false;
    }

    runs_[run_id].state = "completed";
    return true;
}

} // namespace vinox::gui
