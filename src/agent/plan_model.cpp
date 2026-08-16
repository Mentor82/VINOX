#include "vinox/vinox_agent.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <set>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <wincrypt.h>
#endif

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
    return result;
#else
    return "0000000000000000000000000000000000000000000000000000000000000000";
#endif
}

struct vinox_plan {
    nlohmann::json raw_json;
    vinox_plan_status status{VINOX_PLAN_STATUS_DRAFT};
    std::string calculated_hash;
};

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

    if (!j.is_object() || !j.contains("goal") || !j.contains("steps")) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (!j["goal"].is_string() || !j["steps"].is_array() || j["steps"].empty()) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::set<std::string> step_ids;
    for (const auto& step : j["steps"]) {
        if (!step.is_object() || !step.contains("step_id") || !step.contains("description")) {
            return VINOX_STATUS_INVALID_ARGUMENT;
        }
        std::string sid = step["step_id"].get<std::string>();
        if (step_ids.count(sid)) return VINOX_STATUS_INVALID_ARGUMENT; // duplicate step_id
        step_ids.insert(sid);
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
        return VINOX_STATUS_INVALID_ARGUMENT; // Hash mismatch rejection
    }

    plan->status = VINOX_PLAN_STATUS_APPROVED;
    return VINOX_STATUS_OK;
}

} // extern "C"
