#include "vinox/linep.h"
#include "vinox/linep.hpp"
#include "vinox/openvino.h"
#include "vinox/embedding.h"
#include "vinox/embedding.hpp"
#include "vinox/serving.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#define ioctlsocket ioctl
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace vinox {
namespace transport {

namespace {

constexpr uint32_t LINEP_V02_MAGIC = 0x504E4C32; // "2LNP"
constexpr uint8_t LINEP_V02_VERSION_MAJOR = 0;
constexpr uint8_t LINEP_V02_VERSION_MINOR = 2;

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

static_assert(sizeof(WireHeader) == 32, "WireHeader must be 32 bytes");

inline void write_u8(std::vector<uint8_t>& buf, uint8_t val) {
    buf.push_back(val);
}

inline void write_u16(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

inline void write_u32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

inline void write_u64(std::vector<uint8_t>& buf, uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

inline void write_float(std::vector<uint8_t>& buf, float val) {
    uint32_t bits{};
    std::memcpy(&bits, &val, sizeof(float));
    write_u32(buf, bits);
}

inline void write_string_u16(std::vector<uint8_t>& buf, const std::string& str) {
    uint16_t len = static_cast<uint16_t>(str.size());
    write_u16(buf, len);
    if (len > 0) {
        buf.insert(buf.end(), str.data(), str.data() + len);
    }
}

inline void write_string_u32(std::vector<uint8_t>& buf, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    write_u32(buf, len);
    if (len > 0) {
        buf.insert(buf.end(), str.data(), str.data() + len);
    }
}

class BufferReader {
public:
    BufferReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), offset_(0) {}

    bool has_remaining(size_t bytes) const noexcept {
        return (offset_ + bytes) <= size_;
    }

    bool read_u8(uint8_t& out) noexcept {
        if (!has_remaining(1)) return false;
        out = data_[offset_++];
        return true;
    }

    bool read_u16(uint16_t& out) noexcept {
        if (!has_remaining(2)) return false;
        out = static_cast<uint16_t>(data_[offset_]) |
              (static_cast<uint16_t>(data_[offset_ + 1]) << 8);
        offset_ += 2;
        return true;
    }

    bool read_u32(uint32_t& out) noexcept {
        if (!has_remaining(4)) return false;
        out = static_cast<uint32_t>(data_[offset_]) |
              (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
              (static_cast<uint32_t>(data_[offset_ + 2]) << 16) |
              (static_cast<uint32_t>(data_[offset_ + 3]) << 24);
        offset_ += 4;
        return true;
    }

    bool read_u64(uint64_t& out) noexcept {
        if (!has_remaining(8)) return false;
        out = 0;
        for (int i = 0; i < 8; ++i) {
            out |= (static_cast<uint64_t>(data_[offset_ + i]) << (i * 8));
        }
        offset_ += 8;
        return true;
    }

    bool read_float(float& out) noexcept {
        uint32_t bits{};
        if (!read_u32(bits)) return false;
        std::memcpy(&out, &bits, sizeof(float));
        return true;
    }

    bool read_string_u16(std::string& out) {
        uint16_t len{};
        if (!read_u16(len)) return false;
        if (!has_remaining(len)) return false;
        out.assign(reinterpret_cast<const char*>(data_ + offset_), len);
        offset_ += len;
        return true;
    }

    bool read_string_u32(std::string& out) {
        uint32_t len{};
        if (!read_u32(len)) return false;
        if (!has_remaining(len)) return false;
        out.assign(reinterpret_cast<const char*>(data_ + offset_), len);
        offset_ += len;
        return true;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t offset_;
};

bool send_all(SOCKET fd, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int r = send(fd, reinterpret_cast<const char*>(buf + sent), static_cast<int>(len - sent), 0);
        if (r <= 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

bool recv_all(SOCKET fd, uint8_t* buf, size_t len) {
    size_t recvd = 0;
    while (recvd < len) {
        int r = recv(fd, reinterpret_cast<char*>(buf + recvd), static_cast<int>(len - recvd), 0);
        if (r <= 0) return false;
        recvd += static_cast<size_t>(r);
    }
    return true;
}

// Real embedding vector plus its provenance, attached to a type-7 (embedding_result)
// event. There is no fake/default state: a caller only passes this when it holds an
// actual OpenVINO-computed vector.
struct EmbeddingResultData {
    std::string space_id;
    std::string model_id;
    std::string version;
    uint32_t dim{0};
    bool l2_normalized{false};
    bool cosine_metric{true};
    std::vector<float> vector;
};

// error.category per the LiNeP V0.2 wire contract: 0 = none, 1 = transient
// (safe to retry, e.g. busy), 2 = permanent (client/server error, retrying
// as-is will not help).
uint8_t ErrorCategoryForCode(uint32_t code) {
    if (code == 0) return 0;
    if (code == 503) return 1; // busy: transient, retry later
    return 2;
}

constexpr uint8_t LINEP_V02_FLAG_AUTHENTICATED = 0x01;
constexpr size_t LINEP_V02_AUTH_EXTENSION_SIZE = 24;
constexpr size_t LINEP_V02_SESSION_BIND_PAYLOAD_SIZE = 36;

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

inline void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t out_mac[32]) {
    uint8_t k[64] = {0};
    if (key_len > 64) {
        Sha256 s;
        s.init();
        s.update(key, key_len);
        s.finish(k);
    } else if (key && key_len > 0) {
        std::memcpy(k, key, key_len);
    }
    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    for (int i = 0; i < 64; ++i) {
        k_ipad[i] = k[i] ^ 0x36;
        k_opad[i] = k[i] ^ 0x5c;
    }
    uint8_t inner[32];
    Sha256 s_in;
    s_in.init();
    s_in.update(k_ipad, 64);
    if (data && data_len > 0) {
        s_in.update(data, data_len);
    }
    s_in.finish(inner);

    Sha256 s_out;
    s_out.init();
    s_out.update(k_opad, 64);
    s_out.update(inner, 32);
    s_out.finish(out_mac);
}

inline void compute_sl1_mac(
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

inline bool verify_sl1_frame(
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

inline std::vector<uint8_t> sign_wire_frame(
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
    hdr.version_major = LINEP_V02_VERSION_MAJOR;
    hdr.version_minor = LINEP_V02_VERSION_MINOR;
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

inline bool parse_hex_key(const std::string& hex, std::vector<uint8_t>& out_key) {
    if (hex.size() != 64) return false;
    out_key.clear();
    out_key.resize(32);
    for (size_t i = 0; i < 32; ++i) {
        char c1 = hex[i * 2];
        char c2 = hex[i * 2 + 1];
        auto hex_val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int v1 = hex_val(c1);
        int v2 = hex_val(c2);
        if (v1 < 0 || v2 < 0) return false;
        out_key[i] = static_cast<uint8_t>((v1 << 4) | v2);
    }
    return true;
}

inline std::vector<uint8_t> encode_runtime_registration_envelope(
    uint8_t operation,
    uint32_t concurrent_slots,
    uint32_t status_code,
    const std::string& reason,
    const std::vector<uint8_t>& capabilities_frame)
{
    std::vector<uint8_t> b;
    b.push_back(1); // schema major
    b.push_back(0); // schema minor
    b.push_back(operation);
    b.push_back(0); // reserved

    auto put_field = [&b](uint16_t tag, const uint8_t* val, size_t len) {
        write_u16(b, tag);
        write_u32(b, static_cast<uint32_t>(len));
        if (len > 0 && val) {
            b.insert(b.end(), val, val + len);
        }
    };

    if (operation == 1 /* REGISTER_RUNTIME */ || operation == 5 /* CAPACITY_UPDATE */) {
        uint8_t slots_le[4];
        slots_le[0] = static_cast<uint8_t>(concurrent_slots & 0xFF);
        slots_le[1] = static_cast<uint8_t>((concurrent_slots >> 8) & 0xFF);
        slots_le[2] = static_cast<uint8_t>((concurrent_slots >> 16) & 0xFF);
        slots_le[3] = static_cast<uint8_t>((concurrent_slots >> 24) & 0xFF);
        put_field(1, slots_le, 4);
    }

    if (operation == 2 /* RESULT */) {
        uint8_t code_le[4];
        code_le[0] = static_cast<uint8_t>(status_code & 0xFF);
        code_le[1] = static_cast<uint8_t>((status_code >> 8) & 0xFF);
        code_le[2] = static_cast<uint8_t>((status_code >> 16) & 0xFF);
        code_le[3] = static_cast<uint8_t>((status_code >> 24) & 0xFF);
        put_field(2, code_le, 4);
        put_field(3, reinterpret_cast<const uint8_t*>(reason.data()), reason.size());
    }

    if (operation == 1 /* REGISTER_RUNTIME */ && !capabilities_frame.empty()) {
        put_field(4, capabilities_frame.data(), capabilities_frame.size());
    }

    return b;
}

inline bool decode_runtime_registration_result(
    const uint8_t* payload, size_t len,
    uint8_t& out_op, uint32_t& out_status_code, std::string& out_reason)
{
    if (len < 4) return false;
    if (payload[0] != 1 || payload[1] != 0) return false;
    out_op = payload[2];
    out_status_code = 0;
    out_reason.clear();

    BufferReader r(payload + 4, len - 4);
    while (r.has_remaining(6)) {
        uint16_t tag{};
        uint32_t f_len{};
        r.read_u16(tag);
        r.read_u32(f_len);
        if (!r.has_remaining(f_len)) return false;

        if (tag == 2 && f_len == 4) {
            r.read_u32(out_status_code);
        } else if (tag == 3) {
            std::vector<uint8_t> s_buf(f_len);
            for (size_t i = 0; i < f_len; ++i) r.read_u8(s_buf[i]);
            out_reason.assign(reinterpret_cast<const char*>(s_buf.data()), f_len);
        } else {
            uint8_t dummy;
            for (size_t i = 0; i < f_len; ++i) r.read_u8(dummy);
        }
    }
    return true;
}

} // anonymous namespace

struct ActiveSession {
    uint64_t request_id{0};
    uint64_t execution_id{0};
    uint32_t output_id{0};
    std::atomic<bool> cancel_requested{false};
    std::string model_id;
    std::string executed_device;
    vinox_model* active_model{nullptr};
};

struct ClientConnection {
    SOCKET fd{INVALID_SOCKET};
    std::mutex send_mutex;
    std::atomic<bool> active{true};

    bool sl1_active{false};
    uint16_t sl1_key_id{0};
    std::vector<uint8_t> sl1_key;
    Sl1Binding sl1_binding{};
    uint32_t next_outbound_seq{1};
    uint32_t expected_inbound_seq{1};
    MessageDirection outbound_direction{MessageDirection::InitiatorToResponder};
    MessageDirection inbound_direction{MessageDirection::ResponderToInitiator};

    explicit ClientConnection(SOCKET s) : fd(s) {}
    ~ClientConnection() {
        if (fd != INVALID_SOCKET) {
            closesocket(fd);
            fd = INVALID_SOCKET;
        }
    }
};

struct LinepWorker::Impl {
    WorkerConfig config;
    std::atomic<bool> running{false};
    std::atomic<uint32_t> active_jobs{0};
    uint16_t active_port{0};

    SOCKET listen_fd{INVALID_SOCKET};
    std::thread listener_thread;
    std::mutex sessions_mutex;
    std::unordered_map<uint64_t, std::shared_ptr<ActiveSession>> active_sessions;

    std::mutex embedding_mutex;
    std::unique_ptr<vinox::embedding::EmbeddingEngine> embedding_engine;
    std::string embedding_engine_model_path; // path the cached engine was loaded from
    std::thread embedding_preload_thread;

    explicit Impl(const WorkerConfig& cfg) : config(cfg), active_port(cfg.port) {
#ifdef _WIN32
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
    }

    ~Impl() {
        StopServer();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool ResolveHardwareDevice(const std::string& preferred_device, std::string& out_device) {
        vinox_device_info devs[8];
        size_t count = 0;
        char top_device[32] = {0};
        vinox_status st = vinox_devices_query(devs, 8, &count, top_device, sizeof(top_device));

        if (st == VINOX_STATUS_OK && count > 0) {
            if (!preferred_device.empty()) {
                for (size_t i = 0; i < count; ++i) {
                    if (devs[i].is_available && preferred_device == devs[i].device_id) {
                        out_device = preferred_device;
                        return true;
                    }
                }
            }
            if (top_device[0] != '\0') {
                out_device = top_device;
                return true;
            }
        }

        // Fallback to configured target device or CPU if query unavailable
        if (!preferred_device.empty() && (preferred_device == "NPU" || preferred_device == "GPU" || preferred_device == "CPU")) {
            out_device = preferred_device;
        } else if (!config.target_device.empty()) {
            out_device = config.target_device;
        } else {
            out_device = "CPU";
        }
        return true;
    }

    bool EnforceAdmissionControl(const std::string& payload, const std::string& preferred_device, std::string& out_device, std::string& out_err, uint32_t& out_code) {
        if (payload.size() > config.payload_limit_bytes) {
            out_err = "Payload size " + std::to_string(payload.size()) + " bytes exceeds 256 KB governance bound (" + std::to_string(config.payload_limit_bytes) + ")";
            out_code = 413;
            return false;
        }

        uint32_t limit = config.max_concurrent_jobs;
        if (config.host_profile == VINOX_LINEP_PROFILE_BACKGROUND) {
            limit = 1;
        }
        if (active_jobs.load() >= limit) {
            out_err = "busy: worker max capacity reached (active jobs: " + std::to_string(active_jobs.load()) + "/" + std::to_string(limit) + ")";
            out_code = 503;
            return false;
        }

        out_code = 200;
        ResolveHardwareDevice(preferred_device, out_device);
        return true;
    }

    std::vector<fs::path> GetConfiguredSearchRoots() {
        std::vector<fs::path> roots;
        std::error_code ec;

        // 1. Check config.json for configured model path
        fs::path cfg_candidates[] = {
            fs::path("config.json"),
            fs::path("C:/ai/openvino/config.json")
        };

        for (const auto& cp : cfg_candidates) {
            if (fs::exists(cp, ec)) {
                std::ifstream f(cp);
                if (f.is_open()) {
                    try {
                        nlohmann::json j;
                        f >> j;
                        std::string path_str;
                        if (j.contains("models_directory") && j["models_directory"].is_string()) {
                            path_str = j["models_directory"].get<std::string>();
                        } else if (j.contains("models_path") && j["models_path"].is_string()) {
                            path_str = j["models_path"].get<std::string>();
                        } else if (j.contains("runtime") && j["runtime"].is_object()) {
                            if (j["runtime"].contains("models_directory") && j["runtime"]["models_directory"].is_string()) {
                                path_str = j["runtime"]["models_directory"].get<std::string>();
                            } else if (j["runtime"].contains("models_path") && j["runtime"]["models_path"].is_string()) {
                                path_str = j["runtime"]["models_path"].get<std::string>();
                            }
                        }

                        if (!path_str.empty()) {
                            fs::path configured_p(path_str);
                            if (fs::exists(configured_p, ec) && fs::is_directory(configured_p, ec)) {
                                roots.push_back(configured_p);
                                break;
                            }
                        }
                    } catch (...) {}
                }
            }
        }

        // 2. Default fallback roots
        fs::path default_ai("C:/ai/models/OpenVINO");
        if (std::find(roots.begin(), roots.end(), default_ai) == roots.end()) {
            if (fs::exists(default_ai, ec) && fs::is_directory(default_ai, ec)) {
                roots.push_back(default_ai);
            }
        }

        fs::path default_local("models");
        if (std::find(roots.begin(), roots.end(), default_local) == roots.end()) {
            if (fs::exists(default_local, ec) && fs::is_directory(default_local, ec)) {
                roots.push_back(default_local);
            }
        }

        return roots;
    }

    // What the host operator has explicitly released for LiNeP, read from the
    // "linep" section of config.json. An absent/empty list means nothing is
    // released: the worker must advertise and serve nothing by default
    // (ADR 0004 §8 - the local host keeps authority over what leaves it).
    struct LinepServeConfig {
        std::vector<std::string> served_models;
        std::string served_embedding_model;
        std::string embedding_device = "CPU";
    };

    LinepServeConfig ReadLinepServeConfig() {
        LinepServeConfig out;
        std::error_code ec;
        fs::path cfg_candidates[] = {
            fs::path("config.json"),
            fs::path("C:/ai/openvino/config.json")
        };

        for (const auto& cp : cfg_candidates) {
            if (!fs::exists(cp, ec)) continue;
            std::ifstream f(cp);
            if (!f.is_open()) continue;
            try {
                nlohmann::json j;
                f >> j;
                if (j.contains("linep") && j["linep"].is_object()) {
                    const auto& linep_cfg = j["linep"];
                    if (linep_cfg.contains("served_models") && linep_cfg["served_models"].is_array()) {
                        for (const auto& entry : linep_cfg["served_models"]) {
                            if (entry.is_string()) out.served_models.push_back(entry.get<std::string>());
                        }
                    }
                    if (linep_cfg.contains("served_embedding_model") && linep_cfg["served_embedding_model"].is_string()) {
                        out.served_embedding_model = linep_cfg["served_embedding_model"].get<std::string>();
                    }
                    if (linep_cfg.contains("embedding_device") && linep_cfg["embedding_device"].is_string()) {
                        out.embedding_device = linep_cfg["embedding_device"].get<std::string>();
                    }
                }
                break; // first config.json found wins, regardless of content
            } catch (...) {}
        }
        return out;
    }

    // The VINOX model registry is the single authority for "is this directory an
    // actually loadable OpenVINO LLM pipeline" (same scan used by CLI/server).
    // Intersected with the explicit linep.served_models allowlist: a model is
    // only served when both the host operator and the registry agree it is real.
    std::vector<vinox::serving::ModelInfo> ScanServedModelRegistry() {
        std::vector<vinox::serving::ModelInfo> result;
        auto serve_cfg = ReadLinepServeConfig();
        if (serve_cfg.served_models.empty()) return result;

        std::unordered_set<std::string> served_set(serve_cfg.served_models.begin(), serve_cfg.served_models.end());

        try {
            vinox::serving::ModelRegistry registry;
            for (const auto& root : GetConfiguredSearchRoots()) {
                try {
                    registry.scan(root.string());
                } catch (...) { /* best effort: keep scanning remaining roots */ }
            }
            size_t n = registry.count();
            for (size_t i = 0; i < n; ++i) {
                auto info = registry.get_info(i);
                if (served_set.count(info.model_id)) {
                    result.push_back(info);
                }
            }
        } catch (...) {}
        return result;
    }

    std::vector<std::string> GetAvailableLocalModels() {
        std::vector<std::string> models;
        for (const auto& m : ScanServedModelRegistry()) {
            models.push_back(m.model_id);
        }

        if (config.allow_mock_models) {
            if (std::find(models.begin(), models.end(), "linep-conformance-model-v02") == models.end()) {
                models.insert(models.begin(), "linep-conformance-model-v02");
            }
        }
        return models;
    }

    // Lazily loads (once per worker lifetime) the embedding model explicitly
    // released via linep.served_embedding_model. Returns NOT_FOUND if that
    // model isn't configured or isn't present on disk; never falls back to a
    // different model.
    vinox_status EnsureEmbeddingEngineLoaded(const std::string& model_id) {
        std::lock_guard<std::mutex> lock(embedding_mutex);
        if (embedding_engine && embedding_engine->is_valid()) {
            return VINOX_STATUS_OK;
        }

        std::error_code ec;
        std::string model_path;
        for (const auto& root : GetConfiguredSearchRoots()) {
            fs::path candidate = root / model_id;
            if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
                model_path = candidate.string();
                break;
            }
        }
        if (model_path.empty()) {
            return VINOX_STATUS_NOT_FOUND;
        }

        std::string device = ReadLinepServeConfig().embedding_device;
        auto engine = std::make_unique<vinox::embedding::EmbeddingEngine>();
        vinox_status st = engine->load(model_path, device);
        if (st != VINOX_STATUS_OK) {
            return st;
        }
        embedding_engine = std::move(engine);
        embedding_engine_model_path = model_path;
        return VINOX_STATUS_OK;
    }

    vinox_status StartServer() {
        if (running.load()) return VINOX_STATUS_OK;

        listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_fd == INVALID_SOCKET) {
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        int reuse = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config.port);
        inet_pton(AF_INET, config.server_address.c_str(), &addr.sin_addr);

        if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            closesocket(listen_fd);
            listen_fd = INVALID_SOCKET;
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        sockaddr_in actual_addr{};
        socklen_t actual_len = sizeof(actual_addr);
        if (getsockname(listen_fd, reinterpret_cast<sockaddr*>(&actual_addr), &actual_len) == 0) {
            active_port = ntohs(actual_addr.sin_port);
        }

        if (listen(listen_fd, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(listen_fd);
            listen_fd = INVALID_SOCKET;
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        running.store(true);
        listener_thread = std::thread(&Impl::AcceptLoop, this);

        // Preload the released embedding model off the accept path so the first
        // RUNTIME_CAPABILITIES request never pays a cold OpenVINO compile (which
        // can take tens of seconds) and a router with a short readiness timeout
        // does not see the worker as unavailable right after start.
        std::string served_embedding_model = ReadLinepServeConfig().served_embedding_model;
        if (!served_embedding_model.empty()) {
            embedding_preload_thread = std::thread(&Impl::EnsureEmbeddingEngineLoaded, this, served_embedding_model);
        }

        return VINOX_STATUS_OK;
    }

    void StopServer() {
        if (!running.exchange(false)) return;

        if (listen_fd != INVALID_SOCKET) {
            closesocket(listen_fd);
            listen_fd = INVALID_SOCKET;
        }

        if (listener_thread.joinable()) {
            listener_thread.join();
        }
        if (embedding_preload_thread.joinable()) {
            embedding_preload_thread.join();
        }
    }

    void AcceptLoop() {
        while (running.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            SOCKET client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd == INVALID_SOCKET) {
                if (!running.load()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            auto conn = std::make_shared<ClientConnection>(client_fd);
            std::thread(&Impl::HandleClientSocket, this, conn).detach();
        }
    }

    void SendWireEnvelope(std::shared_ptr<ClientConnection> conn, uint8_t env_type, uint64_t req_id, uint64_t exec_id, uint32_t out_id, const std::vector<uint8_t>& payload) {
        if (!conn || !conn->active.load()) return;

        std::lock_guard<std::mutex> lock(conn->send_mutex);
        std::vector<uint8_t> frame;

        if (conn->sl1_active && !conn->sl1_key.empty()) {
            uint32_t seq = conn->next_outbound_seq++;
            frame = sign_wire_frame(
                env_type, req_id, exec_id, out_id, payload,
                conn->sl1_binding, seq, conn->sl1_key_id,
                conn->sl1_key, conn->outbound_direction);
        } else {
            WireHeader hdr{};
            hdr.magic = LINEP_V02_MAGIC;
            hdr.version_major = LINEP_V02_VERSION_MAJOR;
            hdr.version_minor = LINEP_V02_VERSION_MINOR;
            hdr.envelope_type = env_type;
            hdr.flags = 0;
            hdr.request_id = req_id;
            hdr.execution_id = exec_id;
            hdr.output_id = out_id;
            hdr.payload_len = static_cast<uint32_t>(payload.size());

            frame.resize(sizeof(WireHeader) + payload.size());
            std::memcpy(frame.data(), &hdr, sizeof(WireHeader));
            if (!payload.empty()) {
                std::memcpy(frame.data() + sizeof(WireHeader), payload.data(), payload.size());
            }
        }

        if (!send_all(conn->fd, frame.data(), frame.size())) {
            conn->active.store(false);
        }
    }

    std::vector<uint8_t> BuildCapabilitiesPayload() {
        std::vector<uint8_t> payload;

        // 1. supported_profiles (u16 count + u8 array)
        write_u16(payload, 3);
        write_u8(payload, 1); // generate
        write_u8(payload, 2); // chat
        write_u8(payload, 3); // embed

        // 2. max_context_tokens & max_output_tokens
        write_u32(payload, 131072);
        write_u32(payload, 8192);

        // 3. Capability flags
        write_u8(payload, 1); // supports_streaming
        write_u8(payload, 1); // supports_cancellation
        write_u8(payload, 1); // supports_tool_calling
        write_u8(payload, 1); // supports_reasoning_deltas
        write_u8(payload, 1); // supports_structured_messages

        // 4. supported_models (Advertise ONLY models VINOX can actually serve locally)
        std::vector<std::string> models = GetAvailableLocalModels();
        write_u16(payload, static_cast<uint16_t>(models.size()));
        for (const auto& m : models) {
            write_string_u16(payload, m);
        }

        // 5. supported_embedding_spaces (only report once the model named by
        // linep.served_embedding_model has actually finished loading). This must
        // never block: the engine is preloaded from Start() on its own thread, and
        // Capabilities only reports what is already warm. A cold/in-flight load
        // (or none configured) reports 0 spaces rather than stall the caller -
        // a router with a short capabilities timeout must see the worker as
        // reachable immediately after start, even while the embedding model is
        // still compiling in the background.
        vinox_embedding_info emb_info{};
        bool have_emb_info = false;
        char emb_pool[512] = {0};

        {
            std::unique_lock<std::mutex> lock(embedding_mutex, std::try_to_lock);
            if (lock.owns_lock() && embedding_engine && embedding_engine->is_valid()) {
                emb_info.struct_size = sizeof(emb_info);
                have_emb_info = (vinox_embedding_get_info(embedding_engine->get(), &emb_info, emb_pool, sizeof(emb_pool)) == VINOX_STATUS_OK);
            }
        }
        std::string served_embedding_model = have_emb_info ? ReadLinepServeConfig().served_embedding_model : std::string();

        if (have_emb_info) {
            write_u16(payload, 1);
            write_string_u16(payload, emb_info.space_id ? emb_info.space_id : served_embedding_model);
            write_string_u16(payload, emb_info.model_id ? emb_info.model_id : served_embedding_model);
            write_string_u16(payload, "v1");
            write_u32(payload, static_cast<uint32_t>(emb_info.dimension));
            write_u8(payload, emb_info.normalization == VINOX_EMBEDDING_NORM_L2 ? 1 : 0);
            write_u8(payload, 1); // cosine metric
        } else {
            // Report 0 embedding spaces when no real embedding model is configured/available
            write_u16(payload, 0);
        }

        return payload;
    }

    std::vector<uint8_t> BuildCapabilitiesFrame() {
        std::vector<uint8_t> payload = BuildCapabilitiesPayload();
        WireHeader hdr{};
        hdr.magic = LINEP_V02_MAGIC;
        hdr.version_major = LINEP_V02_VERSION_MAJOR;
        hdr.version_minor = LINEP_V02_VERSION_MINOR;
        hdr.envelope_type = 4; // Capabilities
        hdr.flags = 0;
        hdr.request_id = 0;
        hdr.execution_id = 0;
        hdr.output_id = 0;
        hdr.payload_len = static_cast<uint32_t>(payload.size());

        std::vector<uint8_t> frame(sizeof(WireHeader) + payload.size());
        std::memcpy(frame.data(), &hdr, sizeof(WireHeader));
        if (!payload.empty()) {
            std::memcpy(frame.data() + sizeof(WireHeader), payload.data(), payload.size());
        }
        return frame;
    }

    void SendCapabilitiesResponse(std::shared_ptr<ClientConnection> conn, uint64_t req_id) {
        std::vector<uint8_t> payload = BuildCapabilitiesPayload();
        SendWireEnvelope(conn, 4 /* capabilities */, req_id, 0, 0, payload);
    }

    void SendEvent(std::shared_ptr<ClientConnection> conn, uint64_t req_id, uint64_t exec_id, uint32_t out_id, uint64_t event_seq, uint8_t event_type, const std::string& evt_payload, uint8_t outcome = 0, uint32_t err_code = 0, const std::string& err_msg = "", const EmbeddingResultData* embedding = nullptr) {
        std::vector<uint8_t> payload;

        write_u64(payload, event_seq);          // u64 event_seq
        write_u8(payload, event_type);          // u8 event_type
        write_u8(payload, outcome);             // u8 outcome
        write_u8(payload, ErrorCategoryForCode(err_code)); // u8 error.category
        write_u32(payload, err_code);           // u32 error.code
        write_string_u16(payload, err_msg);      // u16 str error.message
        write_string_u16(payload, "");           // u16 str error.backend_diagnostic
        write_string_u32(payload, evt_payload);  // u32 str payload
        uint64_t now_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        write_u64(payload, now_us);              // u64 timestamp_us

        if (event_type == 7 /* embedding_result */ && embedding != nullptr) {
            write_string_u16(payload, embedding->space_id);
            write_string_u16(payload, embedding->model_id);
            write_string_u16(payload, embedding->version);
            write_u32(payload, embedding->dim);
            write_u8(payload, embedding->l2_normalized ? 1 : 0);
            write_u8(payload, embedding->cosine_metric ? 1 : 0);
            write_u32(payload, static_cast<uint32_t>(embedding->vector.size()));

            for (float v : embedding->vector) {
                write_float(payload, v);
            }
        }

        SendWireEnvelope(conn, 2 /* event */, req_id, exec_id, out_id, payload);
    }

    struct StreamCallbackCtx {
        Impl* self;
        std::shared_ptr<ClientConnection> conn;
        std::shared_ptr<ActiveSession> session;
        uint64_t* seq_ptr;
    };

    static int OpenVINOStreamCallback(vinox_stream_channel channel, const char* text, size_t text_size, void* user_data) {
        auto* ctx = static_cast<StreamCallbackCtx*>(user_data);
        if (!ctx || !ctx->session) return 1;

        if (ctx->session->cancel_requested.load() || !ctx->conn->active.load()) {
            return 1; // Cancel generation in OpenVINO pipeline
        }

        if (text == nullptr || text_size == 0) return 0;
        std::string chunk(text, text_size);

        if (channel == VINOX_STREAM_CHANNEL_REASONING) {
            ctx->self->SendEvent(ctx->conn, ctx->session->request_id, ctx->session->execution_id, ctx->session->output_id, (*ctx->seq_ptr)++, 5 /* reasoning_delta */, chunk);
        } else {
            ctx->self->SendEvent(ctx->conn, ctx->session->request_id, ctx->session->execution_id, ctx->session->output_id, (*ctx->seq_ptr)++, 3 /* content_delta */, chunk);
        }
        return 0;
    }

    void DispatchRequestTask(
        std::shared_ptr<ClientConnection> conn,
        WireHeader hdr,
        std::shared_ptr<ActiveSession> session,
        uint8_t profile_val,
        std::string request_payload,
        uint32_t max_tokens,
        float temp)
    {
        uint64_t seq = 1;

        if (profile_val == 3 /* embed */) {
            std::string served_embedding_model = ReadLinepServeConfig().served_embedding_model;
            vinox_status load_st = served_embedding_model.empty()
                ? VINOX_STATUS_NOT_FOUND
                : EnsureEmbeddingEngineLoaded(served_embedding_model);

            if (load_st == VINOX_STATUS_OK) {
                std::string text = request_payload.empty() ? " " : request_payload;
                std::vector<float> vec;
                vinox_status gen_st;
                {
                    std::lock_guard<std::mutex> lock(embedding_mutex);
                    gen_st = embedding_engine->generate(text, vec);
                }

                if (gen_st == VINOX_STATUS_OK) {
                    vinox_embedding_info info{};
                    char pool[512] = {0};
                    bool have_info;
                    {
                        std::lock_guard<std::mutex> lock(embedding_mutex);
                        info.struct_size = sizeof(info);
                        have_info = (vinox_embedding_get_info(embedding_engine->get(), &info, pool, sizeof(pool)) == VINOX_STATUS_OK);
                    }

                    EmbeddingResultData emb;
                    emb.space_id = (have_info && info.space_id) ? info.space_id : served_embedding_model;
                    emb.model_id = (have_info && info.model_id) ? info.model_id : served_embedding_model;
                    emb.version = "v1";
                    emb.dim = static_cast<uint32_t>(vec.size());
                    emb.l2_normalized = have_info && (info.normalization == VINOX_EMBEDDING_NORM_L2);
                    emb.cosine_metric = true;
                    emb.vector = std::move(vec);

                    SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, seq++, 7 /* embedding_result */, "", 0, 0, "", &emb);
                    SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, seq++, 10 /* completed */, "", 1 /* completed */, 200, "");
                } else {
                    std::string err_msg = vinox_embedding_last_error();
                    if (err_msg.empty()) err_msg = "Embedding generation failed";
                    SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, seq++, 12 /* failed */, "", 3 /* failed */, 500, err_msg);
                }
            } else {
                SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, seq++, 12 /* failed */, "", 3 /* failed */, 501, "No embedding model configured/available on host worker");
            }
        } else {
            // 1. Resolve the model against the registry, restricted to what the host
            // operator explicitly released for LiNeP (linep.served_models). A model
            // present on disk but not in that allowlist is treated the same as a
            // model that does not exist (404 below) - the host, not a directory
            // scan, decides what LiNeP can reach (ADR 0004 §8).
            std::string model_path;
            for (const auto& m : ScanServedModelRegistry()) {
                if (m.model_id == session->model_id) {
                    model_path = m.local_path;
                    break;
                }
            }

            // Support mock path ONLY if explicitly allowed by configuration (e.g. conformance test harness)
            if (model_path.empty() && config.allow_mock_models) {
                if (session->model_id == "linep-conformance-model-v02" || session->model_id == "test_mock" || session->model_id == "mock") {
                    model_path = "mock";
                }
            }

            // 404 Rejection if model is unknown / not found on disk
            if (model_path.empty()) {
                std::string err_msg = "Model '" + session->model_id + "' not found or not available on host worker";
                SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, seq++, 12 /* failed */, "", 3 /* failed */, 404, err_msg);

                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    active_sessions.erase(hdr.request_id);
                }
                active_jobs.fetch_sub(1);
                return;
            }

            std::string effective_prompt = request_payload.empty() ? "Hello" : request_payload;

            vinox_model_options options{};
            options.struct_size = sizeof(vinox_model_options);
            options.model_path = model_path.c_str();
            options.device = session->executed_device.c_str();
            options.enable_mmap = 1;
            options.enable_cache = 1;

            vinox_model* ov_model = nullptr;
            vinox_status st = vinox_model_load(&options, &ov_model);

            // Report model loading failure as failed (Type 12) before sending any started event
            if (st != VINOX_STATUS_OK || ov_model == nullptr) {
                std::string err_msg = vinox_openvino_last_error();
                if (err_msg.empty()) err_msg = "Failed to load OpenVINO model pipeline";
                SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, seq++, 12 /* failed */, "", 3 /* failed */, 500, err_msg);

                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    active_sessions.erase(hdr.request_id);
                }
                active_jobs.fetch_sub(1);
                return;
            }

            // 2. Model successfully loaded -> emit STARTED event now
            std::string started_json = "{\"status\":\"started\",\"executed_device\":\"" + session->executed_device + "\",\"model\":\"" + session->model_id + "\"}";
            SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, seq++, 2 /* started */, started_json);

            session->active_model = ov_model;

            vinox_generation_options gen_opts{};
            gen_opts.struct_size = sizeof(vinox_generation_options);
            gen_opts.prompt = effective_prompt.c_str();
            gen_opts.max_new_tokens = max_tokens > 0 ? max_tokens : 64;
            gen_opts.temperature = temp;
            gen_opts.reasoning_mode = (profile_val == 2 /* chat */) ? VINOX_REASONING_TAGGED : VINOX_REASONING_NONE;
            gen_opts.reasoning_start_tag = "<think>";
            gen_opts.reasoning_end_tag = "</think>";
            gen_opts.reasoning_can_disable = 1;

            StreamCallbackCtx cb_ctx{this, conn, session, &seq};
            vinox_status gen_st = vinox_model_generate_stream(ov_model, &gen_opts, OpenVINOStreamCallback, &cb_ctx);
            vinox_model_destroy(ov_model);
            session->active_model = nullptr;

            // 3. Terminal outcome (Completed = 10 with 200, Cancelled = 11 with outcome=2, err_code=499, Failed = 12 with 500)
            if (session->cancel_requested.load()) {
                SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, seq++, 11 /* cancelled */, "", 2 /* cancelled */, 499, "request cancelled");
            } else if (gen_st != VINOX_STATUS_OK && gen_st != VINOX_STATUS_CANCELLED) {
                std::string err_msg = vinox_openvino_last_error();
                if (err_msg.empty()) err_msg = "Generation failed";
                SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, seq++, 12 /* failed */, "", 3 /* failed */, 500, err_msg);
            } else {
                SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, seq++, 10 /* completed */, "", 1 /* completed */, 200, "");
            }
        }

        {
            std::lock_guard<std::mutex> lock(sessions_mutex);
            active_sessions.erase(hdr.request_id);
        }
        active_jobs.fetch_sub(1);
    }

    void HandleClientSocket(std::shared_ptr<ClientConnection> conn) {
        while (running.load() && conn->active.load()) {
            WireHeader hdr{};
            if (!recv_all(conn->fd, reinterpret_cast<uint8_t*>(&hdr), sizeof(WireHeader))) {
                break;
            }

            if (hdr.magic != LINEP_V02_MAGIC) {
                break;
            }

            bool has_auth_ext = (hdr.flags & 0x01) != 0;
            uint8_t auth_ext[24] = {0};
            if (has_auth_ext) { // Wire Auth Extension
                if (!recv_all(conn->fd, auth_ext, 24)) break;
            }

            // Always drain the rest of the frame off the socket before replying or
            // closing, even when the frame will be rejected below. Otherwise the
            // client's still-unread payload bytes make Windows send a TCP RST on
            // close instead of a clean shutdown, which can drop our own reply
            // (observed live as WinError 10054 on the client side).
            std::vector<uint8_t> payload(hdr.payload_len);
            if (hdr.payload_len > 0) {
                if (!recv_all(conn->fd, payload.data(), hdr.payload_len)) break;
            }

            if (config.security_level == VINOX_LINEP_SL0_LOCAL && !conn->sl1_active && has_auth_ext) {
                SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, 1, 12 /* failed */, "", 3 /* failed */, 400, "Worker running in SL0 plain mode; auth extension frames rejected");
                break;
            }

            if (conn->sl1_active && !conn->sl1_key.empty()) {
                if (!has_auth_ext) {
                    SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, 1, 12 /* failed */, "", 3 /* failed */, 401, "auth_required");
                    break;
                }
                uint32_t auth_seq = static_cast<uint32_t>(auth_ext[0]) |
                                   (static_cast<uint32_t>(auth_ext[1]) << 8) |
                                   (static_cast<uint32_t>(auth_ext[2]) << 16) |
                                   (static_cast<uint32_t>(auth_ext[3]) << 24);
                uint16_t key_id = static_cast<uint16_t>(auth_ext[4]) |
                                  (static_cast<uint16_t>(auth_ext[5]) << 8);

                if (auth_seq == 0xFFFFFFFF) {
                    SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, 1, 12 /* failed */, "", 3 /* failed */, 401, "auth_seq_exhausted");
                    break;
                }
                if (auth_seq != conn->expected_inbound_seq) {
                    SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, 1, 12 /* failed */, "", 3 /* failed */, 401, "auth_replay");
                    break;
                }
                if (key_id != conn->sl1_key_id) {
                    SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, 1, 12 /* failed */, "", 3 /* failed */, 401, "unknown_key");
                    break;
                }
                if (!verify_sl1_frame(conn->sl1_key.data(), conn->sl1_key.size(),
                                      hdr, auth_ext, conn->sl1_binding,
                                      conn->inbound_direction,
                                      payload.data(), payload.size())) {
                    SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, 1, 12 /* failed */, "", 3 /* failed */, 401, "auth_invalid");
                    break;
                }
                conn->expected_inbound_seq++;
            }

            BufferReader reader(payload.data(), payload.size());

            switch (hdr.envelope_type) {
            case 4: { // Capabilities
                SendCapabilitiesResponse(conn, hdr.request_id);
                break;
            }
            case 5: { // SessionBind
                // Per LiNeP V0.2: an unsigned bind gets no reply, and a signed bind
                // gets a signed SESSION_BIND confirmation - never a CAPABILITIES
                // frame. A client that wants capabilities sends envelope type 4
                // separately (handled above), independent of bind state.
                if (config.security_level == VINOX_LINEP_SL0_LOCAL && !conn->sl1_active) {
                    // has_auth_ext is already rejected at the connection level above
                    // when running SL0, so a bind that reaches here is a plain,
                    // unsigned SL0 bind: silently accepted, no reply.
                    break;
                }
                SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, 1, 12 /* failed */, "", 3 /* failed */, 401, "SL1+ SessionBind authentication is not implemented for inbound listener on this worker; run it at SL0");
                break;
            }
            case 1: { // Request
                uint8_t profile_val = 0;
                reader.read_u8(profile_val);
                std::string model_id, request_payload;
                reader.read_string_u16(model_id);
                reader.read_string_u32(request_payload);

                uint32_t max_tokens = 0;
                float temp = 0.7f;
                uint8_t stream_req = 1;
                reader.read_u32(max_tokens);
                reader.read_float(temp);
                reader.read_u8(stream_req);

                std::string target_device, err_msg;
                uint32_t adm_code = 0;
                if (!EnforceAdmissionControl(request_payload, "", target_device, err_msg, adm_code)) {
                    SendEvent(conn, hdr.request_id, hdr.execution_id, hdr.output_id, 1, 12 /* failed */, "", 3 /* failed */, adm_code, err_msg);
                    break;
                }

                active_jobs.fetch_add(1);
                auto session = std::make_shared<ActiveSession>();
                session->request_id = hdr.request_id;
                session->execution_id = hdr.execution_id;
                session->output_id = hdr.output_id;
                session->model_id = model_id;
                session->executed_device = target_device;

                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    active_sessions[hdr.request_id] = session;
                }

                // Dispatch per-stream request to thread pool / worker thread for true socket multiplexing & concurrent stream completion
                std::thread(&Impl::DispatchRequestTask, this, conn, hdr, session, profile_val, request_payload, max_tokens, temp).detach();
                break;
            }
            case 3: { // Control
                uint8_t control_type = 0;
                reader.read_u8(control_type);
                uint64_t target_req_id = hdr.request_id;
                uint64_t payload_req_id = 0;
                if (reader.read_u64(payload_req_id) && payload_req_id != 0) {
                    target_req_id = payload_req_id;
                }
                if (control_type == 1) { // Cancel
                    std::shared_ptr<ActiveSession> sess;
                    {
                        std::lock_guard<std::mutex> lock(sessions_mutex);
                        auto it = active_sessions.find(target_req_id);
                        if (it != active_sessions.end()) {
                            sess = it->second;
                        } else {
                            it = active_sessions.find(hdr.request_id);
                            if (it != active_sessions.end()) sess = it->second;
                        }
                    }
                    if (sess) {
                        sess->cancel_requested.store(true);
                        if (sess->active_model) {
                            vinox_model_cancel(sess->active_model);
                        }
                    }
                }
                break;
            }
            default:
                break;
            }
        }
        conn->active.store(false);
    }

    vinox_status DialOutboundLease(const std::string& host, uint16_t port, const std::string& auth_token) {
        SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd == INVALID_SOCKET) return VINOX_STATUS_RUNTIME_ERROR;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            closesocket(fd);
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        auto conn = std::make_shared<ClientConnection>(fd);

        // Parse auth_token into key_id, 32-byte key, and binding
        uint16_t key_id = 1;
        std::vector<uint8_t> sl1_key;
        Sl1Binding sl1_binding{};
        sl1_binding.node_id = 1001;
        sl1_binding.runtime_id = 2001;
        sl1_binding.endpoint_id = 1;
        sl1_binding.control_epoch = 1;
        sl1_binding.lease_token = 0xAABBCCDDEEFF0011ULL;

        std::string token_str = auth_token.empty() ? "VINOX_SL1_LEASE_TOKEN" : auth_token;
        std::vector<std::string> parts;
        {
            std::stringstream ss(token_str);
            std::string item;
            while (std::getline(ss, item, ':')) {
                parts.push_back(item);
            }
        }

        if (parts.size() >= 2) {
            try {
                key_id = static_cast<uint16_t>(std::stoul(parts[0]));
            } catch (...) { key_id = 1; }
            if (parts[1].size() == 64 && parse_hex_key(parts[1], sl1_key)) {
                // hex parsed
            } else {
                Sha256 s; s.init();
                s.update(reinterpret_cast<const uint8_t*>(parts[1].data()), parts[1].size());
                sl1_key.resize(32);
                s.finish(sl1_key.data());
            }
            if (parts.size() >= 7) {
                try {
                    sl1_binding.node_id = std::stoull(parts[2]);
                    sl1_binding.runtime_id = std::stoull(parts[3]);
                    sl1_binding.endpoint_id = static_cast<uint32_t>(std::stoul(parts[4]));
                    sl1_binding.control_epoch = std::stoull(parts[5]);
                    sl1_binding.lease_token = std::stoull(parts[6]);
                } catch (...) {}
            }
        } else {
            key_id = 1;
            if (token_str.size() == 64 && parse_hex_key(token_str, sl1_key)) {
                // hex parsed
            } else {
                Sha256 s; s.init();
                s.update(reinterpret_cast<const uint8_t*>(token_str.data()), token_str.size());
                sl1_key.resize(32);
                s.finish(sl1_key.data());
            }
        }

        conn->sl1_active = true;
        conn->sl1_key_id = key_id;
        conn->sl1_key = sl1_key;
        conn->sl1_binding = sl1_binding;
        conn->next_outbound_seq = 1;
        conn->expected_inbound_seq = 1;
        conn->outbound_direction = MessageDirection::InitiatorToResponder;
        conn->inbound_direction = MessageDirection::ResponderToInitiator;

        // Step 1: Send SESSION_BIND (Envelope 5, auth_seq = 1)
        std::vector<uint8_t> bind_payload;
        write_u64(bind_payload, sl1_binding.node_id);
        write_u64(bind_payload, sl1_binding.runtime_id);
        write_u32(bind_payload, sl1_binding.endpoint_id);
        write_u64(bind_payload, sl1_binding.control_epoch);
        write_u64(bind_payload, sl1_binding.lease_token);

        SendWireEnvelope(conn, 5 /* SESSION_BIND */, 0, 0, 0, bind_payload);

        // Step 2: Await confirmation SESSION_BIND from orchestrator (Envelope 5, auth_seq = 1)
        WireHeader bind_resp_hdr{};
        if (!recv_all(conn->fd, reinterpret_cast<uint8_t*>(&bind_resp_hdr), sizeof(WireHeader))) {
            return VINOX_STATUS_RUNTIME_ERROR;
        }
        if (bind_resp_hdr.magic != LINEP_V02_MAGIC || bind_resp_hdr.envelope_type != 5 || (bind_resp_hdr.flags & LINEP_V02_FLAG_AUTHENTICATED) == 0) {
            return VINOX_STATUS_RUNTIME_ERROR;
        }
        uint8_t bind_auth_ext[24] = {0};
        if (!recv_all(conn->fd, bind_auth_ext, 24)) {
            return VINOX_STATUS_RUNTIME_ERROR;
        }
        std::vector<uint8_t> bind_resp_payload(bind_resp_hdr.payload_len);
        if (bind_resp_hdr.payload_len > 0) {
            if (!recv_all(conn->fd, bind_resp_payload.data(), bind_resp_hdr.payload_len)) {
                return VINOX_STATUS_RUNTIME_ERROR;
            }
        }
        if (!verify_sl1_frame(conn->sl1_key.data(), conn->sl1_key.size(),
                              bind_resp_hdr, bind_auth_ext, conn->sl1_binding,
                              conn->inbound_direction,
                              bind_resp_payload.data(), bind_resp_payload.size())) {
            return VINOX_STATUS_PERMISSION_DENIED;
        }
        uint32_t in_seq1 = static_cast<uint32_t>(bind_auth_ext[0]) |
                          (static_cast<uint32_t>(bind_auth_ext[1]) << 8) |
                          (static_cast<uint32_t>(bind_auth_ext[2]) << 16) |
                          (static_cast<uint32_t>(bind_auth_ext[3]) << 24);
        if (in_seq1 != conn->expected_inbound_seq) {
            return VINOX_STATUS_PERMISSION_DENIED;
        }
        conn->expected_inbound_seq++;

        // Step 3: Send RUNTIME_REGISTER (Envelope 6, Operation 1: REGISTER_RUNTIME, auth_seq = 2)
        uint32_t slots = config.max_concurrent_jobs;
        if (config.host_profile == VINOX_LINEP_PROFILE_BACKGROUND) {
            slots = 1;
        }
        if (slots == 0) slots = 1;

        std::vector<uint8_t> cap_frame = BuildCapabilitiesFrame();
        std::vector<uint8_t> reg_payload = encode_runtime_registration_envelope(1 /* REGISTER_RUNTIME */, slots, 0, "", cap_frame);

        SendWireEnvelope(conn, 6 /* RUNTIME_REGISTER */, 0, 0, 0, reg_payload);

        // Step 4: Await RUNTIME_REGISTER result (Envelope 6, Operation 2: RESULT, auth_seq = 2)
        WireHeader reg_resp_hdr{};
        if (!recv_all(conn->fd, reinterpret_cast<uint8_t*>(&reg_resp_hdr), sizeof(WireHeader))) {
            return VINOX_STATUS_RUNTIME_ERROR;
        }
        if (reg_resp_hdr.magic != LINEP_V02_MAGIC || reg_resp_hdr.envelope_type != 6 || (reg_resp_hdr.flags & LINEP_V02_FLAG_AUTHENTICATED) == 0) {
            return VINOX_STATUS_RUNTIME_ERROR;
        }
        uint8_t reg_auth_ext[24] = {0};
        if (!recv_all(conn->fd, reg_auth_ext, 24)) {
            return VINOX_STATUS_RUNTIME_ERROR;
        }
        std::vector<uint8_t> reg_resp_payload(reg_resp_hdr.payload_len);
        if (reg_resp_hdr.payload_len > 0) {
            if (!recv_all(conn->fd, reg_resp_payload.data(), reg_resp_hdr.payload_len)) {
                return VINOX_STATUS_RUNTIME_ERROR;
            }
        }
        if (!verify_sl1_frame(conn->sl1_key.data(), conn->sl1_key.size(),
                              reg_resp_hdr, reg_auth_ext, conn->sl1_binding,
                              conn->inbound_direction,
                              reg_resp_payload.data(), reg_resp_payload.size())) {
            return VINOX_STATUS_PERMISSION_DENIED;
        }
        uint32_t in_seq2 = static_cast<uint32_t>(reg_auth_ext[0]) |
                          (static_cast<uint32_t>(reg_auth_ext[1]) << 8) |
                          (static_cast<uint32_t>(reg_auth_ext[2]) << 16) |
                          (static_cast<uint32_t>(reg_auth_ext[3]) << 24);
        if (in_seq2 != conn->expected_inbound_seq) {
            return VINOX_STATUS_PERMISSION_DENIED;
        }
        conn->expected_inbound_seq++;

        uint8_t res_op = 0;
        uint32_t res_status = 0;
        std::string res_reason;
        if (!decode_runtime_registration_result(reg_resp_payload.data(), reg_resp_payload.size(), res_op, res_status, res_reason)) {
            return VINOX_STATUS_RUNTIME_ERROR;
        }
        if (res_op != 2 /* RESULT */ || res_status != 200) {
            return VINOX_STATUS_PERMISSION_DENIED;
        }

        // Outbound lease fully bound & registered! Detach receiver thread
        running.store(true);
        std::thread(&Impl::HandleClientSocket, this, conn).detach();
        return VINOX_STATUS_OK;
    }
};

LinepWorker::LinepWorker(const WorkerConfig& config)
    : impl_(std::make_unique<Impl>(config)), config_(config) {}

LinepWorker::~LinepWorker() {
    Stop();
}

vinox_status LinepWorker::Start() {
    return impl_->StartServer();
}

vinox_status LinepWorker::Stop() {
    impl_->StopServer();
    return VINOX_STATUS_OK;
}

bool LinepWorker::IsRunning() const {
    return impl_->running.load();
}

uint16_t LinepWorker::GetActivePort() const {
    return impl_->active_port;
}

vinox_status LinepWorker::DialOutboundLease(
    const std::string& orchestrator_host,
    uint16_t orchestrator_port,
    const std::string& sl1_auth_token)
{
    return impl_->DialOutboundLease(orchestrator_host, orchestrator_port, sl1_auth_token);
}

ExecutionResult LinepWorker::ProcessRequest(
    const std::string& request_id,
    const std::string& model_id,
    const std::string& prompt,
    const std::string& system_prompt,
    const std::string& preferred_device)
{
    ExecutionResult result{};
    auto start_time = std::chrono::high_resolution_clock::now();

    if (!IsRunning()) {
        result.success = false;
        result.error_message = "LinepWorker listener is not running";
        return result;
    }

    std::string combined_payload = system_prompt + prompt;
    std::string target_device, err_msg;
    uint32_t adm_code = 0;

    if (!impl_->EnforceAdmissionControl(combined_payload, preferred_device, target_device, err_msg, adm_code)) {
        result.success = false;
        result.error_message = err_msg;
        return result;
    }

    impl_->active_jobs.fetch_add(1);
    result.executed_device = target_device;

    std::string model_path;
    std::error_code ec;

    fs::path search_roots[] = {
        fs::path("C:/ai/models/OpenVINO") / model_id,
        fs::path("models") / model_id,
        fs::path(model_id)
    };

    for (const auto& sp : search_roots) {
        if (fs::exists(sp, ec) && fs::is_directory(sp, ec)) {
            model_path = sp.string();
            break;
        }
    }

    if (model_path.empty()) {
        if (model_id == "linep-conformance-model-v02" || model_id == "test_mock" || model_id == "mock") {
            model_path = "mock";
        }
    }

    if (model_path.empty()) {
        result.success = false;
        result.error_message = "Model '" + model_id + "' not found or not available";
        impl_->active_jobs.fetch_sub(1);
        return result;
    }

    vinox_model_options options{};
    options.struct_size = sizeof(vinox_model_options);
    options.model_path = model_path.c_str();
    options.device = target_device.c_str();

    vinox_model* ov_model = nullptr;
    vinox_status st = vinox_model_load(&options, &ov_model);
    if (st == VINOX_STATUS_OK && ov_model != nullptr) {
        vinox_generation_options gen_opts{};
        gen_opts.struct_size = sizeof(vinox_generation_options);
        gen_opts.prompt = combined_payload.c_str();
        gen_opts.max_new_tokens = 64;

        std::string accumulated_resp;
        std::string accumulated_reasoning;

        auto stream_cb = [](vinox_stream_channel channel, const char* text, size_t text_size, void* user_data) -> int {
            if (text && text_size > 0) {
                auto* pair = static_cast<std::pair<std::string*, std::string*>*>(user_data);
                if (channel == VINOX_STREAM_CHANNEL_REASONING) {
                    pair->second->append(text, text_size);
                } else {
                    pair->first->append(text, text_size);
                }
            }
            return 0;
        };

        std::pair<std::string*, std::string*> cb_pair{&accumulated_resp, &accumulated_reasoning};
        vinox_model_generate_stream(ov_model, &gen_opts, stream_cb, &cb_pair);
        vinox_model_destroy(ov_model);

        result.response_text = accumulated_resp;
        result.reasoning_text = accumulated_reasoning;
        result.tokens_generated = static_cast<uint32_t>(accumulated_resp.size() / 4 + 1);
        result.success = true;
    } else {
        result.success = false;
        result.error_message = "Failed to load OpenVINO model pipeline";
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    impl_->active_jobs.fetch_sub(1);
    return result;
}

Session0NpuStatus CheckSession0NpuReadiness() {
    Session0NpuStatus res{};
#ifdef _WIN32
    DWORD session_id = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id)) {
        res.session_id = static_cast<uint32_t>(session_id);
        res.is_session0 = (session_id == 0);
    }
#endif

    vinox_device_info devs[8];
    size_t count = 0;
    char top_dev[32] = {0};
    vinox_status st = vinox_devices_query(devs, 8, &count, top_dev, sizeof(top_dev));

    if (st == VINOX_STATUS_OK && count > 0) {
        for (size_t i = 0; i < count; ++i) {
            if (devs[i].is_available && (std::string(devs[i].device_id).find("NPU") != std::string::npos)) {
                res.npu_available = true;
                res.device_name = devs[i].full_name;
                break;
            }
        }
        if (!res.npu_available && top_dev[0] != '\0') {
            res.device_name = top_dev;
        }
    }

    if (res.is_session0) {
        if (res.npu_available) {
            res.status_message = "Session 0 NPU readiness verified: NPU driver accessible under service context";
        } else {
            res.status_message = "Session 0 active: NPU driver restricted or unavailable in non-interactive session; CPU/GPU fallback ready";
        }
    } else {
        if (res.npu_available) {
            res.status_message = "Interactive Session " + std::to_string(res.session_id) + " NPU readiness verified";
        } else {
            res.status_message = "Interactive Session " + std::to_string(res.session_id) + " active; NPU unavailable";
        }
    }
    return res;
}

} // namespace transport
} // namespace vinox

extern "C" {

struct vinox_linep_worker {
    std::unique_ptr<vinox::transport::LinepWorker> cpp_worker;
};

vinox_status vinox_linep_worker_config_init(vinox_linep_worker_config* config) {
    if (config == nullptr) return VINOX_STATUS_INVALID_ARGUMENT;
    std::memset(config, 0, sizeof(vinox_linep_worker_config));
    config->struct_size = sizeof(vinox_linep_worker_config);
    config->server_address = "127.0.0.1";
    config->port = 52425;
    config->security_level = VINOX_LINEP_SL0_LOCAL;
    config->host_profile = VINOX_LINEP_PROFILE_BALANCED;
    config->target_device = "NPU";
    config->max_concurrent_jobs = 4;
    config->payload_limit_bytes = 262144;
    return VINOX_STATUS_OK;
}

vinox_status vinox_linep_worker_create(
    const vinox_linep_worker_config* config,
    vinox_linep_worker** out_worker)
{
    if (config == nullptr || out_worker == nullptr) return VINOX_STATUS_INVALID_ARGUMENT;

    vinox::transport::WorkerConfig cpp_config;
    if (config->server_address) cpp_config.server_address = config->server_address;
    if (config->port > 0) cpp_config.port = config->port;
    cpp_config.security_level = config->security_level;
    cpp_config.host_profile = config->host_profile;
    if (config->target_device) cpp_config.target_device = config->target_device;
    if (config->max_concurrent_jobs > 0) cpp_config.max_concurrent_jobs = config->max_concurrent_jobs;
    if (config->payload_limit_bytes > 0) cpp_config.payload_limit_bytes = config->payload_limit_bytes;
    cpp_config.allow_mock_models = (config->allow_mock_models != 0);

    auto worker_instance = std::make_unique<vinox_linep_worker>();
    worker_instance->cpp_worker = std::make_unique<vinox::transport::LinepWorker>(cpp_config);

    *out_worker = worker_instance.release();
    return VINOX_STATUS_OK;
}

void vinox_linep_worker_destroy(vinox_linep_worker* worker) {
    if (worker) {
        delete worker;
    }
}

vinox_status vinox_linep_worker_start(vinox_linep_worker* worker) {
    if (worker == nullptr || worker->cpp_worker == nullptr) return VINOX_STATUS_INVALID_ARGUMENT;
    return worker->cpp_worker->Start();
}

vinox_status vinox_linep_worker_stop(vinox_linep_worker* worker) {
    if (worker == nullptr || worker->cpp_worker == nullptr) return VINOX_STATUS_INVALID_ARGUMENT;
    return worker->cpp_worker->Stop();
}

int vinox_linep_worker_is_running(const vinox_linep_worker* worker) {
    if (worker == nullptr || worker->cpp_worker == nullptr) return 0;
    return worker->cpp_worker->IsRunning() ? 1 : 0;
}

uint16_t vinox_linep_worker_get_active_port(const vinox_linep_worker* worker) {
    if (worker == nullptr || worker->cpp_worker == nullptr) return 0;
    return worker->cpp_worker->GetActivePort();
}

vinox_status vinox_linep_check_session0_npu_readiness(
    vinox_linep_session0_npu_status* out_status)
{
    if (out_status == nullptr) return VINOX_STATUS_INVALID_ARGUMENT;
    std::memset(out_status, 0, sizeof(vinox_linep_session0_npu_status));
    out_status->struct_size = sizeof(vinox_linep_session0_npu_status);

    auto status = vinox::transport::CheckSession0NpuReadiness();
    out_status->session_id = status.session_id;
    out_status->is_session0 = status.is_session0 ? 1 : 0;
    out_status->npu_available = status.npu_available ? 1 : 0;

#if defined(_WIN32)
    strncpy_s(out_status->device_name, sizeof(out_status->device_name), status.device_name.c_str(), _TRUNCATE);
    strncpy_s(out_status->status_message, sizeof(out_status->status_message), status.status_message.c_str(), _TRUNCATE);
#else
    strncpy(out_status->device_name, status.device_name.c_str(), sizeof(out_status->device_name) - 1);
    strncpy(out_status->status_message, status.status_message.c_str(), sizeof(out_status->status_message) - 1);
#endif

    return VINOX_STATUS_OK;
}

vinox_status vinox_linep_worker_dial_outbound_lease(
    vinox_linep_worker* worker,
    const char* orchestrator_host,
    uint16_t orchestrator_port,
    const char* sl1_auth_token)
{
    if (worker == nullptr || worker->cpp_worker == nullptr || orchestrator_host == nullptr) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }
    std::string token = sl1_auth_token ? sl1_auth_token : "";
    return worker->cpp_worker->DialOutboundLease(orchestrator_host, orchestrator_port, token);
}

vinox_status vinox_linep_worker_dispatch_request(
    vinox_linep_worker* worker,
    const char* request_id,
    const char* model_id,
    const char* prompt,
    const char* system_prompt,
    const char* target_device,
    uint32_t max_tokens,
    float temperature,
    char** out_response_json)
{
    (void)max_tokens;
    (void)temperature;
    if (worker == nullptr || worker->cpp_worker == nullptr || out_response_json == nullptr) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::string req_id = request_id ? request_id : "req-000";
    std::string mod_id = model_id ? model_id : "qwen2.5:3b";
    std::string p_prompt = prompt ? prompt : "";
    std::string s_prompt = system_prompt ? system_prompt : "";
    std::string dev = target_device ? target_device : "";

    auto res = worker->cpp_worker->ProcessRequest(req_id, mod_id, p_prompt, s_prompt, dev);

    std::ostringstream json_builder;
    json_builder << "{\"success\":" << (res.success ? "true" : "false")
                 << ",\"executed_device\":\"" << res.executed_device << "\""
                 << ",\"response\":\"" << res.response_text << "\""
                 << ",\"error\":\"" << res.error_message << "\"}";

    std::string json_str = json_builder.str();
    char* buf = static_cast<char*>(std::malloc(json_str.size() + 1));
    if (!buf) return VINOX_STATUS_RUNTIME_ERROR;
    std::memcpy(buf, json_str.c_str(), json_str.size() + 1);

    *out_response_json = buf;
    return res.success ? VINOX_STATUS_OK : VINOX_STATUS_RUNTIME_ERROR;
}

}
