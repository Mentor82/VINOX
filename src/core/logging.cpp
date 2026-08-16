#include "vinox/logging.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::atomic<uint32_t> g_min_log_level{VINOX_LOG_INFO};
std::mutex g_log_mutex;
std::ofstream g_file_sink;
std::string g_sink_path;

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

std::string redact_string(const std::string& input) {
    if (input.empty()) return "";
    std::string text = input;

    // 1. Redact Authorization Bearer Tokens (Authorization: Bearer <token>)
    static const std::regex bearer_regex(R"((Authorization\s*:\s*Bearer\s+)[^\s"';]+)", std::regex_constants::icase);
    text = std::regex_replace(text, bearer_regex, "$1[REDACTED]");

    // 2. Redact OpenAI / Generic API Keys (sk-[a-zA-Z0-9_-]{8,})
    static const std::regex sk_regex(R"(sk-[a-zA-Z0-9_-]{8,})");
    text = std::regex_replace(text, sk_regex, "sk-[REDACTED]");

    // 3. Redact Key-Value Secrets (password=..., api_key=..., secret=..., token=...)
    static const std::regex kv_secret_regex(R"(((?:password|api_key|secret|access_token|private_key)\s*[:=]\s*)[^\s"';&]+)", std::regex_constants::icase);
    text = std::regex_replace(text, kv_secret_regex, "$1[REDACTED]");

    return text;
}

std::string sanitize_correlation_id(const char* id) {
    if (!id || id[0] == '\0') return "";
    std::string str(id);
    if (str.length() > 64) str = str.substr(0, 64);
    return redact_string(str);
}

}  // namespace

vinox_status vinox_log_event(
    uint32_t level,
    const char* component,
    const char* event_id,
    const vinox_correlation_context* correlation,
    const char* message_kv
) {
    if (level < g_min_log_level.load(std::memory_order_relaxed)) {
        return VINOX_STATUS_OK;
    }

    std::string comp = component ? redact_string(component) : "vinox";
    std::string evt = event_id ? redact_string(event_id) : "event";
    std::string msg = message_kv ? redact_string(message_kv) : "";

    std::string req_id, sess_id, r_id, t_id, op_id;
    if (correlation && correlation->struct_size >= VINOX_CORRELATION_CONTEXT_MIN_SIZE) {
        req_id = sanitize_correlation_id(correlation->request_id);
        sess_id = sanitize_correlation_id(correlation->session_id);
        r_id = sanitize_correlation_id(correlation->run_id);
        t_id = sanitize_correlation_id(correlation->task_id);
        op_id = sanitize_correlation_id(correlation->operation_id);
    }

    std::stringstream ss;
    ss << "{\"timestamp\":\"" << get_iso_timestamp() << "\","
       << "\"level\":\"" << level_to_string(level) << "\","
       << "\"component\":\"" << comp << "\","
       << "\"event\":\"" << evt << "\","
       << "\"event_schema_version\":1";

    if (!req_id.empty()) ss << ",\"request_id\":\"" << req_id << "\"";
    if (!sess_id.empty()) ss << ",\"session_id\":\"" << sess_id << "\"";
    if (!r_id.empty()) ss << ",\"run_id\":\"" << r_id << "\"";
    if (!t_id.empty()) ss << ",\"task_id\":\"" << t_id << "\"";
    if (!op_id.empty()) ss << ",\"operation_id\":\"" << op_id << "\"";
    if (!msg.empty()) ss << ",\"details\":\"" << msg << "\"";
    ss << "}\n";

    std::string json_line = ss.str();

    // FAIL-SAFE SINK EMISSION: SINK ERRORS DO NOT FAIL OPERATIONS
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_file_sink.is_open()) {
        g_file_sink << json_line;
        g_file_sink.flush();
    }

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

    std::string redacted = redact_string(input_text);
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
    g_min_log_level.store(level, std::memory_order_relaxed);
    return VINOX_STATUS_OK;
}

vinox_status vinox_log_get_level(uint32_t* level_out) {
    if (!level_out) return VINOX_STATUS_INVALID_ARGUMENT;
    *level_out = g_min_log_level.load(std::memory_order_relaxed);
    return VINOX_STATUS_OK;
}

vinox_status vinox_log_configure_sink(
    const char* log_file_path,
    uint32_t /* max_file_size_mb */,
    uint32_t /* max_files */
) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_file_sink.is_open()) {
        g_file_sink.close();
    }

    if (log_file_path && log_file_path[0] != '\0') {
        g_file_sink.open(log_file_path, std::ios::out | std::ios::app);
        if (g_file_sink.is_open()) {
            g_sink_path = log_file_path;
        }
    }

    return VINOX_STATUS_OK;
}
