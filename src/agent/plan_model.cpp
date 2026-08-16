#include "vinox/vinox_agent.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdint>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <wincrypt.h>
#endif

static inline uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

// Portable Standard C++ SHA-256 Implementation
static std::string calculate_sha256_portable(const std::string& input) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    uint32_t h0 = 0x6a09e667, h1 = 0xbb67ae85, h2 = 0x3c6ef372, h3 = 0xa54ff53a;
    uint32_t h4 = 0x510e527f, h5 = 0x9b05688c, h6 = 0x1f83d9ab, h7 = 0x5be0cd19;

    uint64_t bitlen = static_cast<uint64_t>(input.size()) * 8;
    std::vector<uint8_t> data(input.begin(), input.end());
    data.push_back(0x80);
    while ((data.size() % 64) != 56) {
        data.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((bitlen >> (i * 8)) & 0xff));
    }

    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(data[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(data[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }

        h0 += a; h1 += b; h2 += c; h3 += d;
        h4 += e; h5 += f; h6 += g; h7 += h;
    }

    std::ostringstream ss;
    uint32_t state[8] = {h0, h1, h2, h3, h4, h5, h6, h7};
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << std::setw(8) << std::setfill('0') << state[i];
    }
    return ss.str();
}

static std::string calculate_sha256(const std::string& input) {
#if defined(_WIN32)
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string result = "";
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            if (CryptHashData(hHash, (const BYTE*)input.data(), (DWORD)input.size(), 0)) {
                DWORD hash_len = 32;
                BYTE hash_buf[32];
                if (CryptGetHashParam(hHash, HP_HASHVAL, hash_buf, &hash_len, 0)) {
                    std::ostringstream ss;
                    for (DWORD i = 0; i < hash_len; ++i) {
                        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash_buf[i];
                    }
                    result = ss.str();
                }
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    if (!result.empty()) return result;
#endif
    return calculate_sha256_portable(input);
}

struct vinox_plan {
    nlohmann::json raw_json;
    vinox_plan_status status{VINOX_PLAN_STATUS_DRAFT};
    std::string calculated_hash;
};

// DFS Helper for cycle detection
static bool has_cycle_dfs(const std::string& node,
                          const std::map<std::string, std::vector<std::string>>& adj,
                          std::map<std::string, int>& visited) {
    visited[node] = 1; // Visiting
    if (adj.find(node) != adj.end()) {
        for (const auto& dep : adj.at(node)) {
            if (visited[dep] == 1) return true; // Cycle detected
            if (visited[dep] == 0) {
                if (has_cycle_dfs(dep, adj, visited)) return true;
            }
        }
    }
    visited[node] = 2; // Visited
    return false;
}

extern "C" {

VINOX_API vinox_plan* VINOX_CALL vinox_plan_create(const char* json_str) {
    if (!json_str) return nullptr;
    try {
        auto j = nlohmann::json::parse(json_str);
        auto plan = new vinox_plan();
        plan->raw_json = j;
        plan->status = VINOX_PLAN_STATUS_DRAFT;

        if (vinox_plan_validate(plan) == VINOX_STATUS_OK) {
            plan->status = VINOX_PLAN_STATUS_READY;
        }

        return plan;
    } catch (...) {
        return nullptr;
    }
}

VINOX_API void VINOX_CALL vinox_plan_destroy(vinox_plan* plan) {
    if (plan) delete plan;
}

VINOX_API vinox_status VINOX_CALL vinox_plan_validate(const vinox_plan* plan) {
    if (!plan) return VINOX_STATUS_INVALID_ARGUMENT;
    const auto& j = plan->raw_json;

    // Conforms strictly to schemas/agent-plan.schema.json
    if (!j.is_object() || !j.contains("goal") || !j.contains("steps")) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (!j["goal"].is_string() || j["goal"].get<std::string>().empty()) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (!j["steps"].is_array() || j["steps"].empty()) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    // Root allowed fields per schemas/agent-plan.schema.json (additionalProperties: false)
    static const std::set<std::string> ALLOWED_ROOT_FIELDS = {
        "goal", "assumptions", "steps", "capabilities_required", "risks", "expected_artifacts", "validations", "budgets"
    };

    for (auto& el : j.items()) {
        if (ALLOWED_ROOT_FIELDS.find(el.key()) == ALLOWED_ROOT_FIELDS.end()) {
            return VINOX_STATUS_INVALID_ARGUMENT; // Additional root property rejected
        }
    }

    std::set<std::string> step_ids;
    std::map<std::string, std::vector<std::string>> adj;

    // Step allowed fields per schemas/agent-plan.schema.json (additionalProperties: false)
    static const std::set<std::string> ALLOWED_STEP_FIELDS = {
        "step_id", "description", "dependencies", "tool_calls", "expected_output"
    };

    for (const auto& step : j["steps"]) {
        if (!step.is_object() || !step.contains("step_id") || !step.contains("description")) {
            return VINOX_STATUS_INVALID_ARGUMENT;
        }

        for (auto& el : step.items()) {
            if (ALLOWED_STEP_FIELDS.find(el.key()) == ALLOWED_STEP_FIELDS.end()) {
                return VINOX_STATUS_INVALID_ARGUMENT; // Additional step property rejected
            }
        }

        std::string sid = step["step_id"].get<std::string>();
        std::string desc = step["description"].get<std::string>();
        if (sid.empty() || desc.empty()) return VINOX_STATUS_INVALID_ARGUMENT;
        if (step_ids.count(sid)) return VINOX_STATUS_INVALID_ARGUMENT; // duplicate step_id

        step_ids.insert(sid);

        if (step.contains("dependencies") && step["dependencies"].is_array()) {
            for (const auto& dep : step["dependencies"]) {
                if (!dep.is_string()) return VINOX_STATUS_INVALID_ARGUMENT;
                adj[sid].push_back(dep.get<std::string>());
            }
        }

        // Validate tool_calls array if present
        if (step.contains("tool_calls") && step["tool_calls"].is_array()) {
            static const std::set<std::string> ALLOWED_TOOL_CALL_FIELDS = {"name", "arguments"};
            for (const auto& tc : step["tool_calls"]) {
                if (!tc.is_object() || !tc.contains("name") || !tc["name"].is_string()) {
                    return VINOX_STATUS_INVALID_ARGUMENT;
                }
                for (auto& tcel : tc.items()) {
                    if (ALLOWED_TOOL_CALL_FIELDS.find(tcel.key()) == ALLOWED_TOOL_CALL_FIELDS.end()) {
                        return VINOX_STATUS_INVALID_ARGUMENT; // Additional tool call property rejected
                    }
                }
            }
        }
    }

    // Check 1: Reject dangling / unknown dependencies
    for (const auto& kv : adj) {
        for (const auto& dep_id : kv.second) {
            if (step_ids.find(dep_id) == step_ids.end()) {
                return VINOX_STATUS_INVALID_ARGUMENT; // Dangling dependency!
            }
        }
    }

    // Check 2: Reject cyclic dependencies via DFS
    std::map<std::string, int> visited;
    for (const auto& sid : step_ids) {
        if (visited[sid] == 0) {
            if (has_cycle_dfs(sid, adj, visited)) {
                return VINOX_STATUS_INVALID_ARGUMENT; // Cycle detected!
            }
        }
    }

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status VINOX_CALL vinox_plan_compute_hash(const vinox_plan* plan, char* hash_buf, size_t hash_buf_sz) {
    if (!plan || !hash_buf || hash_buf_sz < 65) return VINOX_STATUS_INVALID_ARGUMENT;
    std::string canonical = plan->raw_json.dump();
    std::string hash_str = calculate_sha256(canonical);
    if (hash_str.empty()) return VINOX_STATUS_RUNTIME_ERROR;

#if defined(_WIN32)
    strncpy_s(hash_buf, hash_buf_sz, hash_str.c_str(), _TRUNCATE);
#else
    strncpy(hash_buf, hash_str.c_str(), hash_buf_sz - 1);
    hash_buf[hash_buf_sz - 1] = '\0';
#endif

    return VINOX_STATUS_OK;
}

VINOX_API vinox_plan_status VINOX_CALL vinox_plan_get_status(const vinox_plan* plan) {
    if (!plan) return VINOX_PLAN_STATUS_FAILED;
    return plan->status;
}

VINOX_API vinox_status VINOX_CALL vinox_plan_approve(vinox_plan* plan, const char* expected_hash) {
    if (!plan || !expected_hash) return VINOX_STATUS_INVALID_ARGUMENT;
    if (plan->status != VINOX_PLAN_STATUS_READY && plan->status != VINOX_PLAN_STATUS_DRAFT) {
        return VINOX_STATUS_INVALID_STATE;
    }

    char computed_hash[65] = {0};
    vinox_status st = vinox_plan_compute_hash(plan, computed_hash, sizeof(computed_hash));
    if (st != VINOX_STATUS_OK) return st;

    if (std::string(computed_hash) != std::string(expected_hash)) {
        return VINOX_STATUS_INVALID_ARGUMENT; // Cryptographic Hash mismatch rejection
    }

    plan->status = VINOX_PLAN_STATUS_APPROVED;
    return VINOX_STATUS_OK;
}

} // extern "C"
