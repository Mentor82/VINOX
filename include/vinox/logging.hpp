#ifndef VINOX_LOGGING_HPP
#define VINOX_LOGGING_HPP

#include "vinox/logging.h"

#include <string>
#include <vector>

namespace vinox::logging {

class CorrelationScope {
public:
    CorrelationScope(
        const std::string& request_id = "",
        const std::string& session_id = "",
        const std::string& run_id = "",
        const std::string& task_id = "",
        const std::string& operation_id = ""
    ) : req_id_(request_id), sess_id_(session_id), r_id_(run_id), t_id_(task_id), op_id_(operation_id) {
        ctx_.struct_size = sizeof(vinox_correlation_context);
        ctx_.request_id = req_id_.empty() ? nullptr : req_id_.c_str();
        ctx_.session_id = sess_id_.empty() ? nullptr : sess_id_.c_str();
        ctx_.run_id = r_id_.empty() ? nullptr : r_id_.c_str();
        ctx_.task_id = t_id_.empty() ? nullptr : t_id_.c_str();
        ctx_.operation_id = op_id_.empty() ? nullptr : op_id_.c_str();
    }

    const vinox_correlation_context* get_c_ctx() const { return &ctx_; }

    const std::string& request_id() const { return req_id_; }
    const std::string& session_id() const { return sess_id_; }
    const std::string& run_id() const { return r_id_; }
    const std::string& task_id() const { return t_id_; }
    const std::string& operation_id() const { return op_id_; }

private:
    std::string req_id_;
    std::string sess_id_;
    std::string r_id_;
    std::string t_id_;
    std::string op_id_;
    vinox_correlation_context ctx_{};
};

inline std::string redact_secrets(const std::string& input) {
    if (input.empty()) return "";
    size_t req_size = 0;
    vinox_redact_sensitive_text(input.c_str(), nullptr, 0, &req_size);
    if (req_size <= 1) return input;
    std::vector<char> buf(req_size + 16);
    if (vinox_redact_sensitive_text(input.c_str(), buf.data(), buf.size(), nullptr) == VINOX_STATUS_OK) {
        return std::string(buf.data());
    }
    return input;
}

inline void log_info(const std::string& component, const std::string& event_id, const CorrelationScope& scope, const std::string& msg) {
    vinox_log_event(VINOX_LOG_INFO, component.c_str(), event_id.c_str(), scope.get_c_ctx(), msg.c_str());
}

inline void log_error(const std::string& component, const std::string& event_id, const CorrelationScope& scope, const std::string& msg) {
    vinox_log_event(VINOX_LOG_ERROR, component.c_str(), event_id.c_str(), scope.get_c_ctx(), msg.c_str());
}

}  // namespace vinox::logging

#endif /* VINOX_LOGGING_HPP */
