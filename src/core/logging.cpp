#include "vinox/logging.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace {

#define VINOX_FIELD_PRESENT_MEMBER(ptr, member) \
    ((ptr)->struct_size >= (offsetof(std::remove_pointer_t<decltype(ptr)>, member) + sizeof((ptr)->member)))

thread_local std::string tls_last_error;

std::atomic<uint32_t> g_min_log_level{VINOX_LOG_INFO};
std::mutex g_sink_mutex;
std::shared_ptr<spdlog::logger> g_logger;
std::atomic<bool> g_sink_ok{true};
std::atomic<uint64_t> g_dropped_count{0};

std::string get_iso_timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = system_clock::to_time_t(now);
    std::tm bt{};
#if defined(_WIN32)
    gmtime_s(&bt, &timer);
#else
    gmtime_r(&timer, &bt);
#endif
    std::stringstream ss;
    ss << std::put_time(&bt, "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

const char* level_to_string(uint32_t level) {
    switch (level) {
        case VINOX_LOG_TRACE:    return "TRACE";
        case VINOX_LOG_DEBUG:    return "DEBUG";
        case VINOX_LOG_INFO:     return "INFO";
        case VINOX_LOG_WARN:     return "WARN";
        case VINOX_LOG_ERROR:    return "ERROR";
        case VINOX_LOG_CRITICAL: return "CRITICAL";
        default:                 return "INFO";
    }
}

spdlog::level::level_enum to_spdlog_level(uint32_t level) {
    switch (level) {
        case VINOX_LOG_TRACE:    return spdlog::level::trace;
        case VINOX_LOG_DEBUG:    return spdlog::level::debug;
        case VINOX_LOG_INFO:     return spdlog::level::info;
        case VINOX_LOG_WARN:     return spdlog::level::warn;
        case VINOX_LOG_ERROR:    return spdlog::level::err;
        case VINOX_LOG_CRITICAL: return spdlog::level::critical;
        default:                 return spdlog::level::info;
    }
}

std::string redact_string(const std::string& input) {
    if (input.empty()) return "";
    std::string text = input;

    // 1. Redact Authorization Bearer Tokens (Authorization: Bearer <token>)
    static const std::regex bearer_regex(R"((Authorization\s*:\s*Bearer\s+)[^\s"';]+)", std::regex_constants::icase);
    text = std::regex_replace(text, bearer_regex, "$1[REDACTED]");

    // 2. Redact OpenAI / Generic API Keys (sk-[a-zA-Z0-9_-]{8,})
    static const std::regex sk_regex(R"(sk-[a-zA-Z0-9_-]{8,})");
    text = std::regex_replace(text, sk_regex, "sk-[REDACTED]");

    // 3. Redact Key-Value Secrets (password, pass, secret, token, access_token, refresh_token, cookie, auth_token, private_key)
    static const std::regex kv_secret_regex(
        R"(((?:password|pass|secret|token|access_token|refresh_token|cookie|auth_token|private_key|api_key)\s*[:=]\s*)[^\s"';&]+)",
        std::regex_constants::icase
    );
    text = std::regex_replace(text, kv_secret_regex, "$1[REDACTED]");

    return text;
}

std::string sanitize_and_bound_id(const char* id, size_t max_len = 64) {
    if (!id || id[0] == '\0') return "";
    std::string str(id);
    if (str.length() > max_len) str = str.substr(0, max_len);

    // Keep only valid ID characters [a-zA-Z0-9_.-]
    std::string clean;
    clean.reserve(str.length());
    for (char c : str) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-') {
            clean.push_back(c);
        } else {
            clean.push_back('_');
        }
    }
    return redact_string(clean);
}

std::string bound_text(const char* text, size_t max_len = 1024) {
    if (!text || text[0] == '\0') return "";
    std::string str(text);
    if (str.length() > max_len) str = str.substr(0, max_len) + "...[TRUNCATED]";
    return redact_string(str);
}

}  // namespace

void vinox_set_last_error(const char* message) {
    if (!message) {
        tls_last_error.clear();
        return;
    }
    tls_last_error = redact_string(message);
}

const char* vinox_last_error(void) {
    return tls_last_error.c_str();
}

vinox_status vinox_log_event_ex(
    uint32_t level,
    const char* component,
    const char* event_id,
    const vinox_correlation_context* correlation,
    const vinox_log_event_meta* meta
) {
    if (level > VINOX_LOG_CRITICAL) {
        vinox_set_last_error("Invalid log level: exceeds VINOX_LOG_CRITICAL");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (level < g_min_log_level.load(std::memory_order_relaxed)) {
        return VINOX_STATUS_OK;
    }

    std::string comp = bound_text(component ? component : "vinox", 32);
    std::string evt = bound_text(event_id ? event_id : "event", 64);

    std::string req_id, sess_id, r_id, t_id, op_id;
    if (correlation != nullptr) {
        if (VINOX_FIELD_PRESENT_MEMBER(correlation, request_id)) req_id = sanitize_and_bound_id(correlation->request_id);
        if (VINOX_FIELD_PRESENT_MEMBER(correlation, session_id)) sess_id = sanitize_and_bound_id(correlation->session_id);
        if (VINOX_FIELD_PRESENT_MEMBER(correlation, run_id)) r_id = sanitize_and_bound_id(correlation->run_id);
        if (VINOX_FIELD_PRESENT_MEMBER(correlation, task_id)) t_id = sanitize_and_bound_id(correlation->task_id);
        if (VINOX_FIELD_PRESENT_MEMBER(correlation, operation_id)) op_id = sanitize_and_bound_id(correlation->operation_id);
    }

    std::string model_id, backend, status_str, details;
    uint64_t duration_ms = 0;
    uint32_t status_code = 0;
    bool has_duration = false;
    bool has_status_code = false;

    if (meta != nullptr) {
        if (VINOX_FIELD_PRESENT_MEMBER(meta, model_id)) model_id = sanitize_and_bound_id(meta->model_id);
        if (VINOX_FIELD_PRESENT_MEMBER(meta, backend)) backend = sanitize_and_bound_id(meta->backend);
        if (VINOX_FIELD_PRESENT_MEMBER(meta, duration_ms)) { duration_ms = meta->duration_ms; has_duration = true; }
        if (VINOX_FIELD_PRESENT_MEMBER(meta, status)) status_str = bound_text(meta->status, 32);
        if (VINOX_FIELD_PRESENT_MEMBER(meta, status_code)) { status_code = meta->status_code; has_status_code = true; }
        if (VINOX_FIELD_PRESENT_MEMBER(meta, details)) details = bound_text(meta->details, 1024);
    }

    // GUARANTEED NLOHMANN/JSON SPEC-COMPLIANT JSON ESCAPING
    nlohmann::json j;
    j["timestamp"] = get_iso_timestamp();
    j["level"] = level_to_string(level);
    j["component"] = comp;
    j["event"] = evt;
    j["event_schema_version"] = 1;

    if (!req_id.empty()) j["request_id"] = req_id;
    if (!sess_id.empty()) j["session_id"] = sess_id;
    if (!r_id.empty()) j["run_id"] = r_id;
    if (!t_id.empty()) j["task_id"] = t_id;
    if (!op_id.empty()) j["operation_id"] = op_id;

    if (!model_id.empty()) j["model_id"] = model_id;
    if (!backend.empty()) j["backend"] = backend;
    if (has_duration) j["duration_ms"] = duration_ms;
    if (!status_str.empty()) j["status"] = status_str;
    if (has_status_code) j["status_code"] = status_code;
    if (!details.empty()) j["details"] = details;

    std::string json_line = j.dump();

    // SINK EMISSION WITH SPDLOG
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    try {
        if (g_logger) {
            g_logger->log(to_spdlog_level(level), "{}", json_line);
        } else {
            std::cerr << json_line << "\n";
        }
    } catch (...) {
        g_sink_ok.store(false, std::memory_order_relaxed);
        g_dropped_count.fetch_add(1, std::memory_order_relaxed);
    }

    return VINOX_STATUS_OK;
}

vinox_status vinox_log_event(
    uint32_t level,
    const char* component,
    const char* event_id,
    const vinox_correlation_context* correlation,
    const char* message_kv
) {
    vinox_log_event_meta meta{};
    meta.struct_size = sizeof(vinox_log_event_meta);
    meta.details = message_kv;
    return vinox_log_event_ex(level, component, event_id, correlation, &meta);
}

vinox_status vinox_correlation_serialize_envelope(
    const vinox_correlation_context* correlation,
    char* output_buf,
    size_t output_buf_size,
    size_t* required_size_out
) {
    if (!correlation) {
        vinox_set_last_error("correlation context cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    nlohmann::json j;
    j["wire_version"] = 1;

    if (VINOX_FIELD_PRESENT_MEMBER(correlation, request_id) && correlation->request_id)
        j["request_id"] = sanitize_and_bound_id(correlation->request_id);
    if (VINOX_FIELD_PRESENT_MEMBER(correlation, session_id) && correlation->session_id)
        j["session_id"] = sanitize_and_bound_id(correlation->session_id);
    if (VINOX_FIELD_PRESENT_MEMBER(correlation, run_id) && correlation->run_id)
        j["run_id"] = sanitize_and_bound_id(correlation->run_id);
    if (VINOX_FIELD_PRESENT_MEMBER(correlation, task_id) && correlation->task_id)
        j["task_id"] = sanitize_and_bound_id(correlation->task_id);
    if (VINOX_FIELD_PRESENT_MEMBER(correlation, operation_id) && correlation->operation_id)
        j["operation_id"] = sanitize_and_bound_id(correlation->operation_id);

    std::string str = j.dump();
    size_t req_len = str.length() + 1;
    if (required_size_out) *required_size_out = req_len;

    if (output_buf && output_buf_size > 0) {
        size_t copy_len = std::min(str.length(), output_buf_size - 1);
        std::memcpy(output_buf, str.c_str(), copy_len);
        output_buf[copy_len] = '\0';
    }

    return VINOX_STATUS_OK;
}

vinox_status vinox_correlation_deserialize_envelope(
    const char* json_str,
    vinox_correlation_context* correlation_out,
    char* string_pool_buf,
    size_t string_pool_buf_size
) {
    if (!json_str || !correlation_out || !string_pool_buf || string_pool_buf_size == 0) {
        vinox_set_last_error("Invalid arguments for deserializing correlation envelope");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (correlation_out->struct_size < VINOX_CORRELATION_CONTEXT_MIN_SIZE) {
        vinox_set_last_error("correlation_out->struct_size is too small");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_str);
    } catch (...) {
        vinox_set_last_error("Invalid JSON envelope");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    size_t offset = 0;
    auto copy_field = [&](const char* key) -> const char* {
        if (!j.contains(key) || !j[key].is_string()) return nullptr;
        std::string val = j[key].get<std::string>();
        if (offset + val.length() + 1 > string_pool_buf_size) return nullptr;
        char* dst = string_pool_buf + offset;
        std::memcpy(dst, val.c_str(), val.length());
        dst[val.length()] = '\0';
        offset += val.length() + 1;
        return dst;
    };

    correlation_out->request_id = copy_field("request_id");
    if (VINOX_FIELD_PRESENT_MEMBER(correlation_out, session_id)) correlation_out->session_id = copy_field("session_id");
    if (VINOX_FIELD_PRESENT_MEMBER(correlation_out, run_id)) correlation_out->run_id = copy_field("run_id");
    if (VINOX_FIELD_PRESENT_MEMBER(correlation_out, task_id)) correlation_out->task_id = copy_field("task_id");
    if (VINOX_FIELD_PRESENT_MEMBER(correlation_out, operation_id)) correlation_out->operation_id = copy_field("operation_id");

    return VINOX_STATUS_OK;
}

vinox_status vinox_redact_sensitive_text(
    const char* input_text,
    char* output_buf,
    size_t output_buf_size,
    size_t* required_size_out
) {
    if (!input_text) {
        if (output_buf && output_buf_size > 0) output_buf[0] = '\0';
        if (required_size_out) *required_size_out = 1;
        return VINOX_STATUS_OK;
    }

    std::string redacted;
    try {
        redacted = redact_string(input_text);
    } catch (...) {
        redacted = "[REDACTION_FAILED]"; // FAIL CLOSED
    }

    size_t req_len = redacted.length() + 1;
    if (required_size_out) {
        *required_size_out = req_len;
    }

    if (output_buf && output_buf_size > 0) {
        size_t copy_len = std::min(redacted.length(), output_buf_size - 1);
        std::memcpy(output_buf, redacted.c_str(), copy_len);
        output_buf[copy_len] = '\0';
    }

    return VINOX_STATUS_OK;
}

vinox_status vinox_log_set_level(uint32_t level) {
    if (level > VINOX_LOG_CRITICAL) {
        vinox_set_last_error("Invalid log level: level exceeds VINOX_LOG_CRITICAL");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }
    g_min_log_level.store(level, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    if (g_logger) {
        g_logger->set_level(to_spdlog_level(level));
    }
    return VINOX_STATUS_OK;
}

vinox_status vinox_log_get_level(uint32_t* level_out) {
    if (!level_out) return VINOX_STATUS_INVALID_ARGUMENT;
    *level_out = g_min_log_level.load(std::memory_order_relaxed);
    return VINOX_STATUS_OK;
}

vinox_status vinox_log_configure_sink(
    const char* log_file_path,
    uint32_t max_file_size_mb,
    uint32_t max_files
) {
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    try {
        std::vector<spdlog::sink_ptr> sinks;

        // Console sink
        auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        console_sink->set_pattern("%v");
        sinks.push_back(console_sink);

        // File sink if requested
        if (log_file_path && log_file_path[0] != '\0') {
            size_t max_bytes = static_cast<size_t>(max_file_size_mb > 0 ? max_file_size_mb : 10) * 1024 * 1024;
            size_t max_f = max_files > 0 ? max_files : 3;

            auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_file_path, max_bytes, max_f);
            rotating_sink->set_pattern("%v");
            sinks.push_back(rotating_sink);
        }

        auto logger = std::make_shared<spdlog::logger>("vinox_logger", sinks.begin(), sinks.end());
        logger->set_level(to_spdlog_level(g_min_log_level.load(std::memory_order_relaxed)));
        logger->flush_on(spdlog::level::info);

        spdlog::set_default_logger(logger);
        g_logger = logger;
        g_sink_ok.store(true, std::memory_order_relaxed);
        return VINOX_STATUS_OK;
    } catch (const std::exception& e) {
        g_sink_ok.store(false, std::memory_order_relaxed);
        g_dropped_count.fetch_add(1, std::memory_order_relaxed);
        vinox_set_last_error((std::string("Failed to configure log sink: ") + e.what()).c_str());
        return VINOX_STATUS_RUNTIME_ERROR;
    }
}

vinox_status vinox_log_get_sink_status(
    uint32_t* sink_ok_out,
    uint64_t* dropped_count_out
) {
    if (sink_ok_out) *sink_ok_out = g_sink_ok.load(std::memory_order_relaxed) ? 1u : 0u;
    if (dropped_count_out) *dropped_count_out = g_dropped_count.load(std::memory_order_relaxed);
    return VINOX_STATUS_OK;
}
