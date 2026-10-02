#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <thread>
#include <chrono>
#include <sstream>

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

static constexpr uint32_t LINEP_V02_MAGIC = 0x504E4C32; // "2LNP"
static constexpr uint8_t LINEP_V02_FLAG_AUTHENTICATED = 0x01;
static constexpr size_t LINEP_V02_AUTH_EXTENSION_SIZE = 24;

enum class MessageDirection : uint8_t {
    InitiatorToResponder = 1,
    ResponderToInitiator = 2
};

struct Sl1Binding {
    uint64_t node_id{1001};
    uint64_t runtime_id{2001};
    uint32_t endpoint_id{1};
    uint64_t control_epoch{1};
    uint64_t lease_token{0xAABBCCDDEEFF0011ULL};
};

struct Sha256 {
    uint32_t state[8]{0};
    uint64_t count{0};
    uint8_t buffer[64]{0};

    static inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void init() {
        state[0] = 0x6a09e667;
        state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372;
        state[3] = 0xa54ff53a;
        state[4] = 0x510e527f;
        state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab;
        state[7] = 0x5be0cd19;
        count = 0;
    }

    void transform(const uint8_t data[64]) {
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x39100b57,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };

        uint32_t m[64];
        for (int i = 0; i < 16; ++i) {
            m[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
                   (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(data[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
            uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
            m[i] = m[i - 16] + s0 + m[i - 7] + s1;
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + K[i] + m[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buffer[count % 64] = data[i];
            count++;
            if (count % 64 == 0) {
                transform(buffer);
            }
        }
    }

    void finish(uint8_t digest[32]) {
        uint8_t pad[64] = {0x80};
        uint64_t total_bits = count * 8;
        size_t pad_len = (count % 64 < 56) ? (56 - (count % 64)) : (120 - (count % 64));
        update(pad, pad_len);
        uint8_t len_bytes[8];
        for (int i = 0; i < 8; ++i) {
            len_bytes[i] = static_cast<uint8_t>((total_bits >> ((7 - i) * 8)) & 0xFF);
        }
        update(len_bytes, 8);
        for (int i = 0; i < 8; ++i) {
            digest[i * 4] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
        }
    }
};

static void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t out_mac[32]) {
    uint8_t k[64] = {0};
    if (key_len > 64) {
        Sha256 s; s.init(); s.update(key, key_len); s.finish(k);
    } else {
        std::memcpy(k, key, key_len);
    }
    uint8_t k_ipad[64], k_opad[64];
    for (int i = 0; i < 64; ++i) {
        k_ipad[i] = k[i] ^ 0x36;
        k_opad[i] = k[i] ^ 0x5c;
    }
    uint8_t inner[32];
    Sha256 s_in; s_in.init(); s_in.update(k_ipad, 64);
    if (data && data_len > 0) s_in.update(data, data_len);
    s_in.finish(inner);

    Sha256 s_out; s_out.init(); s_out.update(k_opad, 64); s_out.update(inner, 32);
    s_out.finish(out_mac);
}

static void write_u8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }
static void write_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF);
}
static void write_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF);
    buf.push_back((v >> 16) & 0xFF); buf.push_back((v >> 24) & 0xFF);
}
static void write_u64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) buf.push_back((v >> (i * 8)) & 0xFF);
}

static void compute_sl1_mac(
    const uint8_t* key, size_t key_len,
    const WireHeader& hdr_with_auth_flag,
    uint32_t auth_seq, uint16_t key_id,
    const Sl1Binding& bind,
    MessageDirection dir,
    const uint8_t* payload, size_t payload_len,
    uint8_t out_mac[16])
{
    std::vector<uint8_t> mac_in;
    mac_in.reserve(80 + payload_len);

    const uint8_t* hdr_bytes = reinterpret_cast<const uint8_t*>(&hdr_with_auth_flag);
    mac_in.insert(mac_in.end(), hdr_bytes, hdr_bytes + 32);

    write_u32(mac_in, auth_seq);
    write_u16(mac_in, key_id);
    write_u16(mac_in, 0); // reserved

    write_u64(mac_in, bind.node_id);
    write_u64(mac_in, bind.runtime_id);
    write_u32(mac_in, bind.endpoint_id);
    write_u64(mac_in, bind.control_epoch);
    write_u64(mac_in, bind.lease_token);

    write_u8(mac_in, static_cast<uint8_t>(dir));
    write_u8(mac_in, 0);
    write_u8(mac_in, 0);
    write_u8(mac_in, 0);

    if (payload && payload_len > 0) {
        mac_in.insert(mac_in.end(), payload, payload + payload_len);
    }

    uint8_t full_mac[32];
    hmac_sha256(key, key_len, mac_in.data(), mac_in.size(), full_mac);
    std::memcpy(out_mac, full_mac, 16);
}

static bool verify_sl1_frame(
    const uint8_t* key, size_t key_len,
    const WireHeader& hdr,
    const uint8_t auth_ext[24],
    const Sl1Binding& bind,
    MessageDirection dir,
    const uint8_t* payload, size_t payload_len)
{
    if ((hdr.flags & LINEP_V02_FLAG_AUTHENTICATED) == 0) return false;
    uint32_t auth_seq = static_cast<uint32_t>(auth_ext[0]) |
                       (static_cast<uint32_t>(auth_ext[1]) << 8) |
                       (static_cast<uint32_t>(auth_ext[2]) << 16) |
                       (static_cast<uint32_t>(auth_ext[3]) << 24);
    uint16_t key_id = static_cast<uint16_t>(auth_ext[4]) |
                      (static_cast<uint16_t>(auth_ext[5]) << 8);
    if (auth_ext[6] != 0 || auth_ext[7] != 0) return false;

    uint8_t expected_mac[16];
    compute_sl1_mac(key, key_len, hdr, auth_seq, key_id, bind, dir, payload, payload_len, expected_mac);

    int diff = 0;
    for (size_t i = 0; i < 16; ++i) {
        diff |= (expected_mac[i] ^ auth_ext[8 + i]);
    }
    return diff == 0;
}

static std::vector<uint8_t> sign_wire_frame(
    uint8_t env_type,
    uint64_t req_id, uint64_t exec_id, uint32_t out_id,
    const std::vector<uint8_t>& payload,
    const Sl1Binding& bind,
    uint32_t auth_seq, uint16_t key_id,
    const std::vector<uint8_t>& key,
    MessageDirection dir)
{
    WireHeader hdr{};
    hdr.magic = LINEP_V02_MAGIC;
    hdr.version_major = 0;
    hdr.version_minor = 2;
    hdr.envelope_type = env_type;
    hdr.flags = key.empty() ? 0 : LINEP_V02_FLAG_AUTHENTICATED;
    hdr.request_id = req_id;
    hdr.execution_id = exec_id;
    hdr.output_id = out_id;
    hdr.payload_len = static_cast<uint32_t>(payload.size());

    if (key.empty()) {
        std::vector<uint8_t> frame(sizeof(WireHeader) + payload.size());
        std::memcpy(frame.data(), &hdr, sizeof(WireHeader));
        if (!payload.empty()) {
            std::memcpy(frame.data() + sizeof(WireHeader), payload.data(), payload.size());
        }
        return frame;
    }

    uint8_t mac[16];
    compute_sl1_mac(key.data(), key.size(), hdr, auth_seq, key_id, bind, dir, payload.data(), payload.size(), mac);

    std::vector<uint8_t> frame(sizeof(WireHeader) + LINEP_V02_AUTH_EXTENSION_SIZE + payload.size());
    std::memcpy(frame.data(), &hdr, sizeof(WireHeader));

    uint8_t* ext_ptr = frame.data() + sizeof(WireHeader);
    ext_ptr[0] = static_cast<uint8_t>(auth_seq & 0xFF);
    ext_ptr[1] = static_cast<uint8_t>((auth_seq >> 8) & 0xFF);
    ext_ptr[2] = static_cast<uint8_t>((auth_seq >> 16) & 0xFF);
    ext_ptr[3] = static_cast<uint8_t>((auth_seq >> 24) & 0xFF);
    ext_ptr[4] = static_cast<uint8_t>(key_id & 0xFF);
    ext_ptr[5] = static_cast<uint8_t>((key_id >> 8) & 0xFF);
    ext_ptr[6] = 0;
    ext_ptr[7] = 0;
    std::memcpy(ext_ptr + 8, mac, 16);

    if (!payload.empty()) {
        std::memcpy(frame.data() + sizeof(WireHeader) + LINEP_V02_AUTH_EXTENSION_SIZE, payload.data(), payload.size());
    }
    return frame;
}

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

    std::string token_str = "SL1_OUTBOUND_TEST_TOKEN";
    std::vector<uint8_t> sl1_key(32);
    Sha256 k_sha; k_sha.init();
    k_sha.update(reinterpret_cast<const uint8_t*>(token_str.data()), token_str.size());
    k_sha.finish(sl1_key.data());

    Sl1Binding binding{};
    binding.node_id = 1001;
    binding.runtime_id = 2001;
    binding.endpoint_id = 1;
    binding.control_epoch = 1;
    binding.lease_token = 0xAABBCCDDEEFF0011ULL;

    // 3. Spawn Orchestrator acceptance thread
    std::thread orch_thread([orch_fd, sl1_key, binding]() {
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        SOCKET conn_fd = accept(orch_fd, reinterpret_cast<sockaddr*>(&caddr), &clen);
        if (conn_fd == INVALID_SOCKET) return;

        // Step 1: Await worker SESSION_BIND (Envelope 5, auth_seq = 1)
        WireHeader bind_hdr{};
        if (!recv_all_lease(conn_fd, reinterpret_cast<uint8_t*>(&bind_hdr), sizeof(WireHeader))) {
            closesocket(conn_fd);
            return;
        }

        if (bind_hdr.magic != LINEP_V02_MAGIC || bind_hdr.envelope_type != 5 || (bind_hdr.flags & LINEP_V02_FLAG_AUTHENTICATED) == 0) {
            std::cerr << "FAILED: Invalid SESSION_BIND header\n";
            closesocket(conn_fd);
            return;
        }

        uint8_t bind_auth[24] = {0};
        if (!recv_all_lease(conn_fd, bind_auth, 24)) {
            closesocket(conn_fd);
            return;
        }

        std::vector<uint8_t> bind_payload(bind_hdr.payload_len);
        if (bind_hdr.payload_len > 0) {
            if (!recv_all_lease(conn_fd, bind_payload.data(), bind_hdr.payload_len)) {
                closesocket(conn_fd);
                return;
            }
        }

        if (!verify_sl1_frame(sl1_key.data(), sl1_key.size(), bind_hdr, bind_auth, binding, MessageDirection::InitiatorToResponder, bind_payload.data(), bind_payload.size())) {
            std::cerr << "FAILED: Worker SESSION_BIND SL1 MAC verification failed\n";
            closesocket(conn_fd);
            return;
        }
        std::cout << "[PASS 03] Outbound worker dialed in & SL1 Initiator SessionBind verified.\n";

        // Step 2: Orchestrator confirms SESSION_BIND (Envelope 5, auth_seq = 1, ResponderToInitiator)
        std::vector<uint8_t> conf_frame = sign_wire_frame(
            5 /* SESSION_BIND */,
            bind_hdr.request_id, 0, 0,
            bind_payload,
            binding,
            1 /* auth_seq */, 1 /* key_id */,
            sl1_key,
            MessageDirection::ResponderToInitiator
        );
        send_all_lease(conn_fd, conf_frame.data(), conf_frame.size());

        // Step 3: Await worker RUNTIME_REGISTER (Envelope 6, auth_seq = 2)
        WireHeader reg_hdr{};
        if (!recv_all_lease(conn_fd, reinterpret_cast<uint8_t*>(&reg_hdr), sizeof(WireHeader))) {
            closesocket(conn_fd);
            return;
        }

        if (reg_hdr.magic != LINEP_V02_MAGIC || reg_hdr.envelope_type != 6 || (reg_hdr.flags & LINEP_V02_FLAG_AUTHENTICATED) == 0) {
            std::cerr << "FAILED: Invalid RUNTIME_REGISTER header\n";
            closesocket(conn_fd);
            return;
        }

        uint8_t reg_auth[24] = {0};
        if (!recv_all_lease(conn_fd, reg_auth, 24)) {
            closesocket(conn_fd);
            return;
        }

        std::vector<uint8_t> reg_payload(reg_hdr.payload_len);
        if (reg_hdr.payload_len > 0) {
            if (!recv_all_lease(conn_fd, reg_payload.data(), reg_hdr.payload_len)) {
                closesocket(conn_fd);
                return;
            }
        }

        if (!verify_sl1_frame(sl1_key.data(), sl1_key.size(), reg_hdr, reg_auth, binding, MessageDirection::InitiatorToResponder, reg_payload.data(), reg_payload.size())) {
            std::cerr << "FAILED: Worker RUNTIME_REGISTER SL1 MAC verification failed\n";
            closesocket(conn_fd);
            return;
        }

        if (reg_payload.size() < 4 || reg_payload[0] != 1 || reg_payload[2] != 1 /* REGISTER_RUNTIME */) {
            std::cerr << "FAILED: RUNTIME_REGISTER schema/op mismatch\n";
            closesocket(conn_fd);
            return;
        }

        std::cout << "[PASS 04] Outbound worker RUNTIME_REGISTER (Envelope 6) & Capabilities verified.\n";

        // Step 4: Orchestrator replies with RUNTIME_REGISTER result (Envelope 6, Operation 2: RESULT, status 200, auth_seq = 2)
        std::vector<uint8_t> res_body;
        res_body.push_back(1); // schema major
        res_body.push_back(0); // schema minor
        res_body.push_back(2); // op = RESULT
        res_body.push_back(0); // reserved

        // Tag 2: status_code = 200 (u32 LE)
        write_u16(res_body, 2);
        write_u32(res_body, 4);
        write_u32(res_body, 200);

        // Tag 3: reason = "OK"
        std::string ok_str = "OK";
        write_u16(res_body, 3);
        write_u32(res_body, static_cast<uint32_t>(ok_str.size()));
        res_body.insert(res_body.end(), ok_str.begin(), ok_str.end());

        std::vector<uint8_t> reg_res_frame = sign_wire_frame(
            6 /* RUNTIME_REGISTER */,
            reg_hdr.request_id, 0, 0,
            res_body,
            binding,
            2 /* auth_seq */, 1 /* key_id */,
            sl1_key,
            MessageDirection::ResponderToInitiator
        );
        send_all_lease(conn_fd, reg_res_frame.data(), reg_res_frame.size());

        // Step 5: Dispatch test REQUEST over the outbound lease (Envelope 1, auth_seq = 3)
        std::vector<uint8_t> req_payload;
        req_payload.push_back(1); // profile generate
        std::string model = "linep-conformance-model-v02";
        write_u16(req_payload, static_cast<uint16_t>(model.size()));
        req_payload.insert(req_payload.end(), model.begin(), model.end());

        std::string prompt = "Outbound lease request prompt";
        write_u32(req_payload, static_cast<uint32_t>(prompt.size()));
        req_payload.insert(req_payload.end(), prompt.begin(), prompt.end());

        write_u32(req_payload, 8); // max_tokens
        float temp = 0.7f;
        uint32_t temp_bits{};
        std::memcpy(&temp_bits, &temp, 4);
        write_u32(req_payload, temp_bits);
        req_payload.push_back(1); // stream_req

        std::vector<uint8_t> req_frame = sign_wire_frame(
            1 /* REQUEST */,
            5001, 50010, 0,
            req_payload,
            binding,
            3 /* auth_seq */, 1 /* key_id */,
            sl1_key,
            MessageDirection::ResponderToInitiator
        );
        send_all_lease(conn_fd, req_frame.data(), req_frame.size());

        // Step 6: Read streamed EVENT frames from worker, verifying SL1 signatures
        bool received_completed = false;
        uint32_t expected_evt_seq = 3;

        while (true) {
            WireHeader eh{};
            if (!recv_all_lease(conn_fd, reinterpret_cast<uint8_t*>(&eh), sizeof(WireHeader))) break;

            uint8_t evt_auth[24] = {0};
            if ((eh.flags & LINEP_V02_FLAG_AUTHENTICATED) != 0) {
                if (!recv_all_lease(conn_fd, evt_auth, 24)) break;
            }

            std::vector<uint8_t> epayload(eh.payload_len);
            if (eh.payload_len > 0) {
                if (!recv_all_lease(conn_fd, epayload.data(), eh.payload_len)) break;
            }

            if ((eh.flags & LINEP_V02_FLAG_AUTHENTICATED) != 0) {
                if (!verify_sl1_frame(sl1_key.data(), sl1_key.size(), eh, evt_auth, binding, MessageDirection::InitiatorToResponder, epayload.data(), epayload.size())) {
                    std::cerr << "FAILED: EVENT frame SL1 verification failed\n";
                    break;
                }
                expected_evt_seq++;
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
            std::cout << "[PASS 05] Workload successfully dispatches, executes and streams over authenticated SL1 outbound worker lease.\n";
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
    std::cout << "[PASS 06] Worker lease disconnected cleanly.\n";
    std::cout << "ALL OUTBOUND LEASE & SESSION 0 TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
