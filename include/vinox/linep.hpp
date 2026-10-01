#ifndef VINOX_LINEP_HPP
#define VINOX_LINEP_HPP

#include "vinox/linep.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace vinox {
namespace transport {

struct WorkerConfig {
    std::string server_address = "127.0.0.1";
    uint16_t port = 52425;
    vinox_linep_security_level security_level = VINOX_LINEP_SL1_TOKEN;
    vinox_linep_host_profile host_profile = VINOX_LINEP_PROFILE_BALANCED;
    std::string target_device = "NPU";
    uint32_t max_concurrent_jobs = 4;
    uint32_t payload_limit_bytes = 262144; // 256 KB governance payload bound
};

struct ExecutionResult {
    bool success = false;
    std::string response_text;
    std::string reasoning_text;
    std::string error_message;
    std::string executed_device;
    uint32_t tokens_generated = 0;
    double duration_ms = 0.0;
};

class VINOX_API LinepWorker {
public:
    explicit LinepWorker(const WorkerConfig& config);
    ~LinepWorker();

    LinepWorker(const LinepWorker&) = delete;
    LinepWorker& operator=(const LinepWorker&) = delete;

    vinox_status Start();
    vinox_status Stop();
    bool IsRunning() const;
    uint16_t GetActivePort() const;

    ExecutionResult ProcessRequest(
        const std::string& request_id,
        const std::string& model_id,
        const std::string& prompt,
        const std::string& system_prompt = "",
        const std::string& preferred_device = "");

    const WorkerConfig& GetConfig() const { return config_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    WorkerConfig config_;
};

} // namespace transport
} // namespace vinox

#endif // VINOX_LINEP_HPP
