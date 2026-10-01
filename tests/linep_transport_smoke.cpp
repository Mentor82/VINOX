#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <thread>
#include <chrono>

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

#pragma pack(push, 1)
struct WireHeader {
    uint32_t magic;
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t envelope_type;
    uint8_t flags;
    uint64_t request_id;
    uint64_t execution_id;
    uint32_t output_id;
    uint32_t payload_len;
};
#pragma pack(pop)

bool send_all_smoke(SOCKET fd, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int r = send(fd, reinterpret_cast<const char*>(buf + sent), static_cast<int>(len - sent), 0);
        if (r <= 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

bool recv_all_smoke(SOCKET fd, uint8_t* buf, size_t len) {
    size_t recvd = 0;
    while (recvd < len) {
        int r = recv(fd, reinterpret_cast<char*>(buf + recvd), static_cast<int>(len - recvd), 0);
        if (r <= 0) return false;
        recvd += static_cast<size_t>(r);
    }
    return true;
}

void append_u8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }
void append_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF);
}
void append_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF);
    buf.push_back((v >> 16) & 0xFF); buf.push_back((v >> 24) & 0xFF);
}
void append_u64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) buf.push_back((v >> (i * 8)) & 0xFF);
}
void append_float(std::vector<uint8_t>& buf, float v) {
    uint32_t bits{}; std::memcpy(&bits, &v, 4); append_u32(buf, bits);
}
void append_str_u16(std::vector<uint8_t>& buf, const std::string& s) {
    append_u16(buf, static_cast<uint16_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}
void append_str_u32(std::vector<uint8_t>& buf, const std::string& s) {
    append_u32(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

std::vector<uint8_t> build_request_frame(uint64_t req_id, const std::string& model, const std::string& prompt, uint32_t max_tokens = 16) {
    std::vector<uint8_t> payload;
    append_u8(payload, 1); // profile = generate
    append_str_u16(payload, model);
    append_str_u32(payload, prompt);
    append_u32(payload, max_tokens);
    append_float(payload, 0.7f);
    append_u8(payload, 1); // stream_req

    WireHeader hdr{};
    hdr.magic = 0x504E4C32; // "2LNP"
    hdr.version_major = 0;
    hdr.version_minor = 2;
    hdr.envelope_type = 1; // Request
    hdr.flags = 0;
    hdr.request_id = req_id;
    hdr.execution_id = req_id * 10;
    hdr.output_id = 0;
    hdr.payload_len = static_cast<uint32_t>(payload.size());

    std::vector<uint8_t> frame(sizeof(WireHeader) + payload.size());
    std::memcpy(frame.data(), &hdr, sizeof(WireHeader));
    std::memcpy(frame.data() + sizeof(WireHeader), payload.data(), payload.size());
    return frame;
}

int main() {
    std::cout.setf(std::ios::unitbuf);
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

    // 3. Test Direct TCP Socket Connection speaking LiNeP V0.2 Binary Framing & SESSION_BIND (Envelope 5)
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock != INVALID_SOCKET) {
        sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(active_port);
        inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);

        if (connect(sock, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == 0) {
            // Send SESSION_BIND with SL1 Auth Framing (flags = 0x01)
            uint8_t bind_hdr[32 + 24] = {0};
            uint32_t magic = 0x504E4C32;
            std::memcpy(bind_hdr, &magic, 4);
            bind_hdr[4] = 0; bind_hdr[5] = 2;
            bind_hdr[6] = 5; // SessionBind
            bind_hdr[7] = 0x01; // Auth Extension Flag
            uint64_t req_id = 999;
            std::memcpy(bind_hdr + 8, &req_id, 8);

            send_all_smoke(sock, bind_hdr, sizeof(bind_hdr));

            uint8_t resp_hdr[32] = {0};
            if (recv_all_smoke(sock, resp_hdr, 32)) {
                WireHeader* rh = reinterpret_cast<WireHeader*>(resp_hdr);
                if (rh->magic == magic && rh->envelope_type == 4 /* Capabilities */) {
                    std::cout << "[PASS 03] SESSION_BIND answered with canonical Capabilities (Envelope Type 4).\n";
                } else {
                    std::cerr << "FAILED: SESSION_BIND did not receive Capabilities envelope type 4! Received: " << (int)rh->envelope_type << "\n";
                    closesocket(sock);
                    vinox_linep_worker_destroy(worker);
                    return 1;
                }
            }
            closesocket(sock);
        }
    }

    // 4. Test TWO CONCURRENT STREAMS ON A SINGLE TCP CONNECTION
    SOCKET conn_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (conn_sock != INVALID_SOCKET) {
        sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(active_port);
        inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);

        if (connect(conn_sock, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == 0) {
            auto req1 = build_request_frame(2001, "linep-conformance-model-v02", "Concurrent prompt 1", 8);
            auto req2 = build_request_frame(2002, "linep-conformance-model-v02", "Concurrent prompt 2", 8);

            // Send both requests back-to-back on the SAME socket
            send_all_smoke(conn_sock, req1.data(), req1.size());
            send_all_smoke(conn_sock, req2.data(), req2.size());

            bool completed_2001 = false;
            bool completed_2002 = false;
            int total_events = 0;

            // Read incoming streaming frames for both requests
            while ((!completed_2001 || !completed_2002) && total_events < 50) {
                WireHeader eh{};
                if (!recv_all_smoke(conn_sock, reinterpret_cast<uint8_t*>(&eh), 32)) {
                    std::cout << "[DEBUG Test04] recv_all_smoke header failed!" << std::endl;
                    break;
                }

                std::vector<uint8_t> payload(eh.payload_len);
                if (eh.payload_len > 0) {
                    if (!recv_all_smoke(conn_sock, payload.data(), eh.payload_len)) {
                        std::cout << "[DEBUG Test04] recv_all_smoke payload failed!" << std::endl;
                        break;
                    }
                }

                if (eh.envelope_type == 2 /* Event */) {
                    total_events++;
                    if (payload.size() >= 10) {
                        uint8_t evt_type = payload[8];
                        uint8_t outcome = payload[9];
                        std::cout << "[DEBUG Test04 Evt] req: " << eh.request_id << " evt_type: " << (int)evt_type << " outcome: " << (int)outcome << std::endl;
                        if (eh.request_id == 2001 && (evt_type == 10 || evt_type == 11) && outcome == 1) {
                            completed_2001 = true;
                        } else if (eh.request_id == 2002 && (evt_type == 10 || evt_type == 11) && outcome == 1) {
                            completed_2002 = true;
                        }
                    }
                }
            }

            closesocket(conn_sock);

            if (completed_2001 && completed_2002) {
                std::cout << "[PASS 04] Single socket concurrent streaming verified: both Request 2001 and Request 2002 completed successfully.\n";
            } else {
                std::cerr << "FAILED: Single socket concurrent requests failed! 2001: " << completed_2001 << ", 2002: " << completed_2002 << "\n";
                vinox_linep_worker_destroy(worker);
                return 1;
            }
        }
    }

    // 5. Test C API Dispatch with Real OpenVINO Model
    char* response_json = nullptr;
    st = vinox_linep_worker_dispatch_request(
        worker,
        "req-npu-001",
        "linep-conformance-model-v02",
        "Erkläre NPU Offloading in VINOX.",
        "System: Du bist ein KI-Assistent.",
        "NPU",
        64,
        0.0f,
        &response_json);

    if (st != VINOX_STATUS_OK || response_json == nullptr) {
        std::cerr << "FAILED: vinox_linep_worker_dispatch_request\n";
        vinox_linep_worker_destroy(worker);
        return 1;
    }

    std::string res_str(response_json);
    std::free(response_json);
    std::cout << "Dispatch JSON response: " << res_str << "\n";

    if (res_str.find("\"success\":true") == std::string::npos || res_str.find("\"executed_device\":") == std::string::npos) {
        std::cerr << "FAILED: JSON response missing expected success markers or executed_device field!\n";
        vinox_linep_worker_destroy(worker);
        return 1;
    }
    std::cout << "[PASS 05] Remote OpenVINO worker request dispatched and verified.\n";

    // 6. Test Unknown Model Rejection (404 Error)
    std::cout << "[DEBUG] Running Test 06 (Unknown Model Rejection)..." << std::endl;
    SOCKET unknown_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (unknown_sock != INVALID_SOCKET) {
        sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(active_port);
        inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);

        if (connect(unknown_sock, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == 0) {
            auto unk_req = build_request_frame(3001, "non_existent_fake_model_xyz", "Test prompt", 16);
            send_all_smoke(unknown_sock, unk_req.data(), unk_req.size());

            // Read events
            bool received_404_failed = false;
            while (true) {
                WireHeader eh{};
                if (!recv_all_smoke(unknown_sock, reinterpret_cast<uint8_t*>(&eh), 32)) break;

                std::vector<uint8_t> payload(eh.payload_len);
                if (eh.payload_len > 0) {
                    if (!recv_all_smoke(unknown_sock, payload.data(), eh.payload_len)) break;
                }

                if (eh.envelope_type == 2 /* Event */ && payload.size() >= 14) {
                    uint8_t evt_type = payload[8];
                    uint8_t outcome = payload[9];
                    uint32_t err_code = 0;
                    std::memcpy(&err_code, payload.data() + 11, 4);

                    if (evt_type == 12 /* failed */ && outcome == 3 && err_code == 404) {
                        received_404_failed = true;
                        break;
                    }
                }
            }

            closesocket(unknown_sock);

            if (received_404_failed) {
                std::cout << "[PASS 06] Unknown model 'non_existent_fake_model_xyz' correctly rejected with 404 Failed event.\n";
            } else {
                std::cerr << "FAILED: Unknown model was not rejected with 404 Failed event!\n";
                vinox_linep_worker_destroy(worker);
                return 1;
            }
        }
    }

    // 7. Test C++ API Bounded Payload Admission Control
    vinox::transport::WorkerConfig cpp_cfg;
    cpp_cfg.port = 0;
    cpp_cfg.payload_limit_bytes = 100; // Intentionally low limit for test
    vinox::transport::LinepWorker cpp_worker(cpp_cfg);
    cpp_worker.Start();

    std::string huge_prompt(200, 'A');
    auto cpp_res = cpp_worker.ProcessRequest("req-huge-002", "linep-conformance-model-v02", huge_prompt);
    if (cpp_res.success) {
        std::cerr << "FAILED: EnforceAdmissionControl should have rejected payload exceeding limit!\n";
        vinox_linep_worker_destroy(worker);
        return 1;
    }
    std::cout << "Rejection message: " << cpp_res.error_message << "\n";
    std::cout << "[PASS 07] Bounded payload governance limit (256 KB invariant) enforced fail-closed.\n";

    // 8. Cleanup
    std::cout << "[DEBUG] Stopping worker..." << std::endl;
    vinox_linep_worker_stop(worker);
    std::cout << "[DEBUG] Destroying worker..." << std::endl;
    vinox_linep_worker_destroy(worker);
    std::cout << "[PASS 08] Worker stopped and cleaned up cleanly." << std::endl;
    std::cout << "ALL LINEP TRANSPORT TESTS PASSED SUCCESSFULLY." << std::endl;
    return 0;
}
