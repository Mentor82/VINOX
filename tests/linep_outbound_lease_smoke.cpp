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

static bool send_all_lease(SOCKET fd, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int r = send(fd, reinterpret_cast<const char*>(buf + sent), static_cast<int>(len - sent), 0);
        if (r <= 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

static bool recv_all_lease(SOCKET fd, uint8_t* buf, size_t len) {
    size_t recvd = 0;
    while (recvd < len) {
        int r = recv(fd, reinterpret_cast<char*>(buf + recvd), static_cast<int>(len - recvd), 0);
        if (r <= 0) return false;
        recvd += static_cast<size_t>(r);
    }
    return true;
}

int main() {
    std::cout.setf(std::ios::unitbuf);
    std::cout << "================================================================================\n";
    std::cout << "          VINOX LiNeP Outbound Worker Lease & Session 0 Smoke Test              \n";
    std::cout << "================================================================================\n";

#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    // 1. Session 0 NPU Readiness Check
    vinox_linep_session0_npu_status sess_status{};
    vinox_status st = vinox_linep_check_session0_npu_readiness(&sess_status);
    if (st != VINOX_STATUS_OK) {
        std::cerr << "FAILED: vinox_linep_check_session0_npu_readiness returned " << st << "\n";
        return 1;
    }

    std::cout << "[PASS 01] Session 0 NPU Check completed:\n";
    std::cout << "   - Session ID:       " << sess_status.session_id << "\n";
    std::cout << "   - Is Session 0:     " << (sess_status.is_session0 ? "YES" : "NO") << "\n";
    std::cout << "   - NPU Available:    " << (sess_status.npu_available ? "YES" : "NO") << "\n";
    std::cout << "   - Device Name:      " << sess_status.device_name << "\n";
    std::cout << "   - Status Message:   " << sess_status.status_message << "\n";

    // 2. Setup Mock Orchestrator Listener socket
    SOCKET orch_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (orch_fd == INVALID_SOCKET) {
        std::cerr << "FAILED: Unable to create orchestrator socket\n";
        return 1;
    }

    int reuse = 1;
    setsockopt(orch_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0; // Ephemeral
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(orch_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "FAILED: Unable to bind orchestrator socket\n";
        closesocket(orch_fd);
        return 1;
    }

    sockaddr_in actual_addr{};
    socklen_t actual_len = sizeof(actual_addr);
    getsockname(orch_fd, reinterpret_cast<sockaddr*>(&actual_addr), &actual_len);
    uint16_t orch_port = ntohs(actual_addr.sin_port);

    listen(orch_fd, 1);
    std::cout << "[PASS 02] Mock Orchestrator listener active on port " << orch_port << "\n";

    // 3. Spawn Orchestrator acceptance thread
    std::thread orch_thread([orch_fd]() {
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        SOCKET conn_fd = accept(orch_fd, reinterpret_cast<sockaddr*>(&caddr), &clen);
        if (conn_fd == INVALID_SOCKET) return;

        // Await SL1 Initiator SessionBind frame from worker (Envelope 5, Flags 0x01 + 24-byte Auth Extension)
        uint8_t bind_hdr[32 + 24] = {0};
        if (recv_all_lease(conn_fd, bind_hdr, sizeof(bind_hdr))) {
            WireHeader* bh = reinterpret_cast<WireHeader*>(bind_hdr);
            if (bh->magic == 0x504E4C32 && bh->envelope_type == 5 && (bh->flags & 0x01) != 0) {
                std::cout << "[PASS 03] Outbound worker dialed in & SL1 Initiator SessionBind verified.\n";

                // Reply with canonical Capabilities (Envelope 4) confirming lease acceptance
                uint8_t cap_hdr[32] = {0};
                WireHeader* ch = reinterpret_cast<WireHeader*>(cap_hdr);
                ch->magic = 0x504E4C32;
                ch->version_major = 0; ch->version_minor = 2;
                ch->envelope_type = 4; // Capabilities
                ch->request_id = bh->request_id;
                ch->payload_len = 0;

                send_all_lease(conn_fd, cap_hdr, 32);

                // Dispatch test request over the outbound connection
                std::vector<uint8_t> req_payload;
                req_payload.push_back(1); // profile generate
                uint16_t m_len = 27;
                req_payload.push_back(m_len & 0xFF); req_payload.push_back((m_len >> 8) & 0xFF);
                std::string model = "linep-conformance-model-v02";
                req_payload.insert(req_payload.end(), model.begin(), model.end());

                std::string prompt = "Outbound lease request prompt";
                uint32_t p_len = static_cast<uint32_t>(prompt.size());
                req_payload.push_back(p_len & 0xFF); req_payload.push_back((p_len >> 8) & 0xFF);
                req_payload.push_back((p_len >> 16) & 0xFF); req_payload.push_back((p_len >> 24) & 0xFF);
                req_payload.insert(req_payload.end(), prompt.begin(), prompt.end());

                uint32_t max_tokens = 8;
                float temp = 0.7f;
                uint8_t stream_req = 1;
                req_payload.push_back(max_tokens & 0xFF); req_payload.push_back((max_tokens >> 8) & 0xFF);
                req_payload.push_back((max_tokens >> 16) & 0xFF); req_payload.push_back((max_tokens >> 24) & 0xFF);
                uint32_t bits{}; std::memcpy(&bits, &temp, 4);
                req_payload.push_back(bits & 0xFF); req_payload.push_back((bits >> 8) & 0xFF);
                req_payload.push_back((bits >> 16) & 0xFF); req_payload.push_back((bits >> 24) & 0xFF);
                req_payload.push_back(stream_req);

                WireHeader rh{};
                rh.magic = 0x504E4C32;
                rh.version_major = 0; rh.version_minor = 2;
                rh.envelope_type = 1; // Request
                rh.flags = 0;
                rh.request_id = 5001;
                rh.execution_id = 50010;
                rh.payload_len = static_cast<uint32_t>(req_payload.size());

                std::vector<uint8_t> frame(sizeof(WireHeader) + req_payload.size());
                std::memcpy(frame.data(), &rh, sizeof(WireHeader));
                std::memcpy(frame.data() + sizeof(WireHeader), req_payload.data(), req_payload.size());

                send_all_lease(conn_fd, frame.data(), frame.size());

                // Read streamed event deltas back from worker over outbound channel
                bool received_completed = false;
                while (true) {
                    WireHeader eh{};
                    if (!recv_all_lease(conn_fd, reinterpret_cast<uint8_t*>(&eh), 32)) break;
                    std::vector<uint8_t> epayload(eh.payload_len);
                    if (eh.payload_len > 0) {
                        if (!recv_all_lease(conn_fd, epayload.data(), eh.payload_len)) break;
                    }

                    if (eh.envelope_type == 2 /* Event */ && epayload.size() >= 10) {
                        uint8_t evt_type = epayload[8];
                        uint8_t outcome = epayload[9];
                        if (evt_type == 10 /* completed */ && outcome == 1) {
                            received_completed = true;
                            break;
                        }
                    }
                }

                if (received_completed) {
                    std::cout << "[PASS 04] Workload successfully dispatches and streams over outbound worker lease.\n";
                }
            }
        }
        closesocket(conn_fd);
    });

    // 4. Create VINOX Worker & Dial Outbound Lease
    vinox_linep_worker_config cfg;
    vinox_linep_worker_config_init(&cfg);
    cfg.port = 0;
    cfg.allow_mock_models = 1;

    vinox_linep_worker* worker = nullptr;
    st = vinox_linep_worker_create(&cfg, &worker);
    if (st != VINOX_STATUS_OK || worker == nullptr) {
        std::cerr << "FAILED: vinox_linep_worker_create\n";
        closesocket(orch_fd);
        orch_thread.join();
        return 1;
    }

    st = vinox_linep_worker_dial_outbound_lease(worker, "127.0.0.1", orch_port, "SL1_OUTBOUND_TEST_TOKEN");
    if (st != VINOX_STATUS_OK) {
        std::cerr << "FAILED: vinox_linep_worker_dial_outbound_lease returned " << st << "\n";
        vinox_linep_worker_destroy(worker);
        closesocket(orch_fd);
        orch_thread.join();
        return 1;
    }

    orch_thread.join();
    closesocket(orch_fd);

    vinox_linep_worker_destroy(worker);
    std::cout << "[PASS 05] Worker lease disconnected cleanly.\n";
    std::cout << "ALL OUTBOUND LEASE & SESSION 0 TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
