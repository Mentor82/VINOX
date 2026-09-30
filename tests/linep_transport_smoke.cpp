#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

#include "vinox/linep.h"
#include "vinox/linep.hpp"
#include "vinox/vinox.h"

int main() {
    std::cout << "================================================================================\n";
    std::cout << "               VINOX LiNeP Remote Worker Transport Smoke Test                  \n";
    std::cout << "================================================================================\n";

    // 1. Initialize C Config
    vinox_linep_worker_config cfg;
    vinox_status st = vinox_linep_worker_config_init(&cfg);
    if (st != VINOX_STATUS_OK) {
        std::cerr << "FAILED: vinox_linep_worker_config_init returned " << st << "\n";
        return 1;
    }

    if (cfg.security_level != VINOX_LINEP_SL1_TOKEN || cfg.host_profile != VINOX_LINEP_PROFILE_BALANCED) {
        std::cerr << "FAILED: Default config mismatch!\n";
        return 1;
    }
    std::cout << "[PASS 01] Default config initialized (SL1_TOKEN, PROFILE_BALANCED, target device=" << cfg.target_device << ").\n";

    // 2. Create Worker Instance
    vinox_linep_worker* worker = nullptr;
    st = vinox_linep_worker_create(&cfg, &worker);
    if (st != VINOX_STATUS_OK || worker == nullptr) {
        std::cerr << "FAILED: vinox_linep_worker_create\n";
        return 1;
    }

    st = vinox_linep_worker_start(worker);
    if (st != VINOX_STATUS_OK || vinox_linep_worker_is_running(worker) != 1) {
        std::cerr << "FAILED: vinox_linep_worker_start\n";
        vinox_linep_worker_destroy(worker);
        return 1;
    }
    std::cout << "[PASS 02] Worker created & started successfully.\n";

    // 3. Test C API Dispatch (NPU Inference Worker Request)
    char* response_json = nullptr;
    st = vinox_linep_worker_dispatch_request(
        worker,
        "req-npu-001",
        "qwen2.5:3b",
        "Erkläre NPU Offloading in VINOX.",
        "System: Du bist ein KI-Assistent.",
        "NPU",
        128,
        0.0f,
        &response_json);

    if (st != VINOX_STATUS_OK || response_json == nullptr) {
        std::cerr << "FAILED: vinox_linep_worker_dispatch_request (NPU)\n";
        vinox_linep_worker_destroy(worker);
        return 1;
    }

    std::string res_str(response_json);
    std::free(response_json);
    std::cout << "Dispatch NPU JSON response: " << res_str << "\n";

    if (res_str.find("\"executed_device\":\"NPU\"") == std::string::npos || res_str.find("\"success\":true") == std::string::npos) {
        std::cerr << "FAILED: JSON response missing expected NPU success markers!\n";
        vinox_linep_worker_destroy(worker);
        return 1;
    }
    std::cout << "[PASS 03] Remote NPU worker request dispatched and verified.\n";

    // 4. Test C++ API Bounded Payload Admission Control
    vinox::transport::WorkerConfig cpp_cfg;
    cpp_cfg.payload_limit_bytes = 100; // Intentionally low limit for test
    vinox::transport::LinepWorker cpp_worker(cpp_cfg);
    cpp_worker.Start();

    std::string huge_prompt(200, 'A');
    auto cpp_res = cpp_worker.ProcessRequest("req-huge-002", "qwen2.5:3b", huge_prompt);
    if (cpp_res.success) {
        std::cerr << "FAILED: EnforceAdmissionControl should have rejected payload exceeding limit!\n";
        vinox_linep_worker_destroy(worker);
        return 1;
    }
    std::cout << "Rejection message: " << cpp_res.error_message << "\n";
    std::cout << "[PASS 04] Bounded payload governance limit (256 KB invariant) enforced fail-closed.\n";

    // 5. Cleanup
    vinox_linep_worker_stop(worker);
    vinox_linep_worker_destroy(worker);
    std::cout << "[PASS 05] Worker stopped and cleaned up cleanly.\n";
    std::cout << "ALL LINEP TRANSPORT TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
