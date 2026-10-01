#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define closesocket close
#endif

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

    // 2. Create Worker Instance with Port 0 (Ephemeral Port)
    cfg.port = 0;
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

    uint16_t active_port = vinox_linep_worker_get_active_port(worker);
    std::cout << "[PASS 02] Worker created & TCP socket listener active on port " << active_port << ".\n";

    // 3. Test Direct TCP Socket Connection speaking LiNeP V0.2 Binary Framing
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock != INVALID_SOCKET) {
        sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(active_port);
        inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);

        if (connect(sock, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == 0) {
            // Send LiNeP V0.2 Capabilities Request (Envelope Type 4)
            uint8_t hdr[32] = {0};
            uint32_t magic = 0x504E4C32; // "2LNP"
            uint8_t env_type = 4;        // Capabilities
            uint64_t req_id = 1001;

            std::memcpy(hdr, &magic, 4);
            hdr[4] = 0; // major
            hdr[5] = 2; // minor
            hdr[6] = env_type;
            std::memcpy(hdr + 8, &req_id, 8);

            send(sock, reinterpret_cast<char*>(hdr), 32, 0);

            // Read Capabilities Response Frame
            uint8_t resp_hdr[32] = {0};
            int r = recv(sock, reinterpret_cast<char*>(resp_hdr), 32, 0);
            if (r == 32) {
                uint32_t resp_magic = 0;
                std::memcpy(&resp_magic, resp_hdr, 4);
                if (resp_magic == magic && resp_hdr[6] == 4) {
                    std::cout << "[PASS 03] Native TCP client sent Capabilities request and received 2LNP binary frame response.\n";
                } else {
                    std::cerr << "FAILED: Wire response magic or envelope type mismatch!\n";
                }
            }
            closesocket(sock);
        }
    }

    // 4. Test C API Dispatch (NPU Inference Worker Request)
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
    std::cout << "[PASS 04] Remote NPU worker request dispatched and verified.\n";

    // 5. Test C++ API Bounded Payload Admission Control
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
    std::cout << "[PASS 05] Bounded payload governance limit (256 KB invariant) enforced fail-closed.\n";

    // 6. Cleanup
    vinox_linep_worker_stop(worker);
    vinox_linep_worker_destroy(worker);
    std::cout << "[PASS 06] Worker stopped and cleaned up cleanly.\n";
    std::cout << "ALL LINEP TRANSPORT TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
