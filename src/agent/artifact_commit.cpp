#include "vinox/vinox_agent.h"
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cstdint>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <wincrypt.h>
#endif

namespace fs = std::filesystem;

static inline uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

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

// Compute deterministic SHA-256 snapshot hash of all regular files in a target directory
static std::string compute_target_snapshot_hash(const fs::path& target_path) {
    std::error_code ec;
    fs::path abs_target = fs::absolute(target_path, ec);
    if (ec || !fs::exists(abs_target)) {
        return calculate_sha256("EMPTY_TARGET_DIRECTORY");
    }

    std::map<std::string, std::string> file_entries;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(abs_target, ec)) {
            if (ec) break;
            if (entry.is_regular_file()) {
                fs::path rel = fs::relative(entry.path(), abs_target, ec);
                std::string rel_str = ec ? entry.path().filename().string() : rel.generic_string();
                std::ifstream in(entry.path(), std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                file_entries[rel_str] = content;
            }
        }
    } catch (...) {}

    if (file_entries.empty()) {
        return calculate_sha256("EMPTY_TARGET_DIRECTORY");
    }

    std::ostringstream ss;
    for (const auto& kv : file_entries) {
        ss << kv.first << ":" << kv.second << "\n";
    }

    return calculate_sha256(ss.str());
}

static bool is_contained_path(const fs::path& root, const fs::path& file_path) {
    try {
        fs::path canonical_root = fs::weakly_canonical(root);
        fs::path canonical_file = fs::weakly_canonical(file_path);

        std::string root_str = canonical_root.string();
        std::string file_str = canonical_file.string();

        std::replace(root_str.begin(), root_str.end(), '\\', '/');
        std::replace(file_str.begin(), file_str.end(), '\\', '/');

        if (root_str.back() != '/') root_str.push_back('/');

        return (file_str.rfind(root_str, 0) == 0 || file_str == root_str.substr(0, root_str.length() - 1));
    } catch (...) {
        return false;
    }
}

extern "C" {

VINOX_API vinox_status VINOX_CALL vinox_artifact_commit_diff(const char* overlay_dir, const char* target_dir, char* diff_buf, size_t diff_buf_sz) {
    if (!overlay_dir || !target_dir || !diff_buf || diff_buf_sz < 1) return VINOX_STATUS_INVALID_ARGUMENT;

    std::ostringstream diff_stream;
    fs::path overlay_path(overlay_dir);
    fs::path target_path(target_dir);

    // Compute target snapshot hash for Review->Apply conflict detection
    std::string target_snapshot = compute_target_snapshot_hash(target_path);
    diff_stream << "SNAPSHOT:" << target_snapshot << "\n";

    if (!fs::exists(overlay_path)) {
        diff_stream << "No changes in overlay directory.\n";
    } else {
        for (const auto& entry : fs::recursive_directory_iterator(overlay_path)) {
            if (entry.is_regular_file()) {
                fs::path rel = fs::relative(entry.path(), overlay_path);
                fs::path target_file = target_path / rel;

                diff_stream << "--- a/" << rel.string() << "\n";
                diff_stream << "+++ b/" << rel.string() << "\n";

                if (!fs::exists(target_file)) {
                    diff_stream << "@@ -0,0 +1 @@\n";
                    std::ifstream in(entry.path(), std::ios::binary);
                    std::string line;
                    while (std::getline(in, line)) {
                        diff_stream << "+" << line << "\n";
                    }
                } else {
                    diff_stream << "@@ modified @@\n";
                    std::ifstream in(entry.path(), std::ios::binary);
                    std::string line;
                    while (std::getline(in, line)) {
                        diff_stream << "+" << line << "\n";
                    }
                }
            }
        }
    }

    std::string res = diff_stream.str();
    if (res.length() >= diff_buf_sz) return VINOX_STATUS_OUT_OF_RANGE;

#if defined(_WIN32)
    strncpy_s(diff_buf, diff_buf_sz, res.c_str(), _TRUNCATE);
#else
    strncpy(diff_buf, res.c_str(), diff_buf_sz - 1);
    diff_buf[diff_buf_sz - 1] = '\0';
#endif

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status VINOX_CALL vinox_artifact_commit_apply_snapshot(const char* overlay_dir, const char* target_dir, const char* expected_snapshot_hash) {
    if (!overlay_dir || !target_dir) return VINOX_STATUS_INVALID_ARGUMENT;

    // Nephy Finding 5: Mandatory Snapshot Binding for Reviewed Takeover
    if (!expected_snapshot_hash || strlen(expected_snapshot_hash) == 0) {
        return VINOX_STATUS_INVALID_ARGUMENT; // Unreviewed takeover without snapshot hash rejected!
    }

    fs::path overlay_path(overlay_dir);
    fs::path target_path(target_dir);

    std::string current_snapshot = compute_target_snapshot_hash(target_path);
    if (current_snapshot != std::string(expected_snapshot_hash)) {
        return VINOX_STATUS_INVALID_STATE; // Conflict detected: target workspace was modified since review!
    }

    if (!fs::exists(overlay_path)) return VINOX_STATUS_OK;

    fs::path staging_tmp = target_path.string() + "_staging_tmp";
    fs::path backup_tmp = target_path.string() + "_backup_tmp";

    try {
        if (fs::exists(staging_tmp)) fs::remove_all(staging_tmp);
        if (fs::exists(backup_tmp)) fs::remove_all(backup_tmp);

        fs::create_directories(staging_tmp);

        // 1. Copy existing target directory into staging directory
        if (fs::exists(target_path)) {
            for (const auto& entry : fs::recursive_directory_iterator(target_path)) {
                if (entry.is_regular_file()) {
                    fs::path rel = fs::relative(entry.path(), target_path);
                    fs::path dest = staging_tmp / rel;

                    if (!is_contained_path(staging_tmp, dest)) {
                        fs::remove_all(staging_tmp);
                        return VINOX_STATUS_INVALID_ARGUMENT; // Path escape rejected!
                    }

                    fs::create_directories(dest.parent_path());
                    fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
                }
            }
        }

        // 2. Apply overlay files into staging directory & verify path containment
        for (const auto& entry : fs::recursive_directory_iterator(overlay_path)) {
            if (entry.is_regular_file()) {
                fs::path rel = fs::relative(entry.path(), overlay_path);
                fs::path dest = staging_tmp / rel;

                if (!is_contained_path(staging_tmp, dest)) {
                    fs::remove_all(staging_tmp);
                    return VINOX_STATUS_INVALID_ARGUMENT; // Path escape rejected!
                }

                fs::create_directories(dest.parent_path());
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
            }
        }

        // 3. Perform atomic backup & swap
        if (fs::exists(target_path)) {
            fs::rename(target_path, backup_tmp);
        }

        try {
            fs::rename(staging_tmp, target_path);
        } catch (...) {
            if (fs::exists(backup_tmp)) {
                fs::rename(backup_tmp, target_path);
            }
            if (fs::exists(staging_tmp)) {
                fs::remove_all(staging_tmp);
            }
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        if (fs::exists(backup_tmp)) {
            fs::remove_all(backup_tmp);
        }

        return VINOX_STATUS_OK;
    } catch (...) {
        if (fs::exists(backup_tmp) && !fs::exists(target_path)) {
            try { fs::rename(backup_tmp, target_path); } catch (...) {}
        }
        if (fs::exists(staging_tmp)) {
            try { fs::remove_all(staging_tmp); } catch (...) {}
        }
        return VINOX_STATUS_RUNTIME_ERROR;
    }
}

VINOX_API vinox_status VINOX_CALL vinox_artifact_commit_apply(const char* overlay_dir, const char* target_dir) {
    return vinox_artifact_commit_apply_snapshot(overlay_dir, target_dir, NULL);
}

} // extern "C"
