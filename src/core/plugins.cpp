#include "vinox/plugins.h"
#include "vinox/plugins.hpp"
#include "vinox/logging.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

thread_local std::string g_plugins_last_error;

void set_plugins_last_error(const std::string& err) {
    g_plugins_last_error = vinox::logging::redact_secrets(err);
}

// SHA256 helper implementation (compact self-contained RFC 6234)
struct Sha256Context {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void sha256_transform(Sha256Context* ctx, const uint8_t data[64]) {
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

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

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

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(Sha256Context* ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

void sha256_update(Sha256Context* ctx, const uint8_t* data, size_t len) {
    size_t idx = static_cast<size_t>(ctx->count & 0x3F);
    ctx->count += len;
    while (len > 0) {
        size_t part = 64 - idx;
        if (len < part) part = len;
        std::memcpy(&ctx->buffer[idx], data, part);
        data += part;
        len -= part;
        idx += part;
        if (idx == 64) {
            sha256_transform(ctx, ctx->buffer);
            idx = 0;
        }
    }
}

std::string sha256_final(Sha256Context* ctx) {
    uint8_t pad[64] = {0x80};
    uint8_t len_bytes[8];
    uint64_t bits = ctx->count * 8;
    for (int i = 0; i < 8; ++i) {
        len_bytes[7 - i] = static_cast<uint8_t>(bits >> (i * 8));
    }
    size_t pad_len = (ctx->count % 64 < 56) ? (56 - (ctx->count % 64)) : (120 - (ctx->count % 64));
    sha256_update(ctx, pad, pad_len);
    sha256_update(ctx, len_bytes, 8);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) {
        ss << std::setw(8) << ctx->state[i];
    }
    return ss.str();
}

std::string calculate_file_sha256(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    Sha256Context ctx;
    sha256_init(&ctx);
    char buf[4096];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        sha256_update(&ctx, reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(f.gcount()));
    }
    return sha256_final(&ctx);
}

// Helper: safe copy string to pool
const char* copy_to_pool(const std::string& str, char* pool_buf, size_t pool_buf_size, size_t& offset) {
    if (offset + str.length() + 1 > pool_buf_size) return nullptr;
    char* dst = pool_buf + offset;
    std::memcpy(dst, str.c_str(), str.length());
    dst[str.length()] = '\0';
    offset += str.length() + 1;
    return dst;
}

// -------------------------------------------------------------
// std_fs Plugin Implementation (Scoped to workspace_root with canonical containment check)
// -------------------------------------------------------------
struct FsPluginState {
    std::filesystem::path root_canonical;
};

bool check_path_contained(const std::filesystem::path& root, const std::string& subpath, std::filesystem::path& out_resolved) {
    try {
        std::filesystem::path combined = root / std::filesystem::path(subpath);
        std::filesystem::path target = std::filesystem::weakly_canonical(combined);
        
        auto r_it = root.begin();
        auto t_it = target.begin();
        for (; r_it != root.end() && t_it != target.end(); ++r_it, ++t_it) {
            if (*r_it != *t_it) return false;
        }
        if (r_it != root.end()) return false;
        out_resolved = target;
        return true;
    } catch (...) {
        return false;
    }
}

vinox_status fs_read_handler(const vinox_tool_call_request* request, vinox_tool_call_result* result_out, char* pool_buf, size_t pool_buf_size, void* user_data) {
    auto* state = static_cast<FsPluginState*>(user_data);
    if (!state || !request || !result_out) return VINOX_STATUS_INVALID_ARGUMENT;

    nlohmann::json args;
    try {
        args = nlohmann::json::parse(request->arguments_json ? request->arguments_json : "{}");
    } catch (...) {
        result_out->status_code = 400;
        result_out->error_message = "Malformed JSON arguments";
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::string rel_path = args.value("path", "");
    std::filesystem::path target;
    if (!check_path_contained(state->root_canonical, rel_path, target)) {
        result_out->status_code = 403;
        result_out->error_message = "Path traversal denied: Target is outside workspace root";
        return VINOX_STATUS_PERMISSION_DENIED;
    }

    std::ifstream file(target, std::ios::binary);
    if (!file.is_open()) {
        result_out->status_code = 404;
        result_out->error_message = "File not found";
        return VINOX_STATUS_NOT_FOUND;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    if (content.size() > 1048576) {
        content = content.substr(0, 1048576); // Truncate at 1MB safe bound
    }

    nlohmann::json res;
    res["path"] = rel_path;
    res["bytes"] = content.size();
    res["content"] = content;
    std::string out_str = res.dump();

    size_t off = 0;
    const char* c_res = copy_to_pool(out_str, pool_buf, pool_buf_size, off);
    if (!c_res) {
        result_out->status_code = 500;
        result_out->error_message = "Pool buffer exhausted";
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    result_out->status_code = 0;
    result_out->result_json = c_res;
    return VINOX_STATUS_OK;
}

vinox_status fs_write_handler(const vinox_tool_call_request* request, vinox_tool_call_result* result_out, char* pool_buf, size_t pool_buf_size, void* user_data) {
    auto* state = static_cast<FsPluginState*>(user_data);
    if (!state || !request || !result_out) return VINOX_STATUS_INVALID_ARGUMENT;

    nlohmann::json args;
    try {
        args = nlohmann::json::parse(request->arguments_json ? request->arguments_json : "{}");
    } catch (...) {
        result_out->status_code = 400;
        result_out->error_message = "Malformed JSON arguments";
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::string rel_path = args.value("path", "");
    std::string content = args.value("content", "");

    std::filesystem::path target;
    if (!check_path_contained(state->root_canonical, rel_path, target)) {
        result_out->status_code = 403;
        result_out->error_message = "Path traversal denied: Target is outside workspace root";
        return VINOX_STATUS_PERMISSION_DENIED;
    }

    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path());
    }

    std::ofstream file(target, std::ios::binary);
    if (!file.is_open()) {
        result_out->status_code = 500;
        result_out->error_message = "Failed to open file for writing";
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    file << content;
    file.close();

    nlohmann::json res;
    res["path"] = rel_path;
    res["bytes_written"] = content.size();
    res["success"] = true;
    std::string out_str = res.dump();

    size_t off = 0;
    const char* c_res = copy_to_pool(out_str, pool_buf, pool_buf_size, off);
    if (!c_res) return VINOX_STATUS_RUNTIME_ERROR;

    result_out->status_code = 0;
    result_out->result_json = c_res;
    return VINOX_STATUS_OK;
}

vinox_status fs_list_handler(const vinox_tool_call_request* request, vinox_tool_call_result* result_out, char* pool_buf, size_t pool_buf_size, void* user_data) {
    auto* state = static_cast<FsPluginState*>(user_data);
    if (!state || !request || !result_out) return VINOX_STATUS_INVALID_ARGUMENT;

    nlohmann::json args;
    try {
        args = nlohmann::json::parse(request->arguments_json ? request->arguments_json : "{}");
    } catch (...) {
        args = nlohmann::json::object();
    }

    std::string rel_path = args.value("path", ".");
    std::filesystem::path target;
    if (!check_path_contained(state->root_canonical, rel_path, target)) {
        result_out->status_code = 403;
        result_out->error_message = "Path traversal denied: Target is outside workspace root";
        return VINOX_STATUS_PERMISSION_DENIED;
    }

    if (!std::filesystem::exists(target) || !std::filesystem::is_directory(target)) {
        result_out->status_code = 404;
        result_out->error_message = "Directory not found";
        return VINOX_STATUS_NOT_FOUND;
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto& entry : std::filesystem::directory_iterator(target)) {
        nlohmann::json item;
        item["name"] = entry.path().filename().string();
        item["is_directory"] = entry.is_directory();
        if (entry.is_regular_file()) item["size"] = entry.file_size();
        items.push_back(item);
    }

    nlohmann::json res;
    res["path"] = rel_path;
    res["entries"] = items;
    std::string out_str = res.dump();

    size_t off = 0;
    const char* c_res = copy_to_pool(out_str, pool_buf, pool_buf_size, off);
    if (!c_res) return VINOX_STATUS_RUNTIME_ERROR;

    result_out->status_code = 0;
    result_out->result_json = c_res;
    return VINOX_STATUS_OK;
}

// -------------------------------------------------------------
// std_math Plugin Implementation (Strict Bounded Calculator Grammar)
// -------------------------------------------------------------
struct MathParser {
    std::string src;
    size_t pos{0};

    explicit MathParser(std::string s) : src(std::move(s)) {}

    void skip_ws() {
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
    }

    double parse_expression() {
        double val = parse_term();
        while (true) {
            skip_ws();
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) {
                char op = src[pos++];
                double next_val = parse_term();
                if (op == '+') val += next_val;
                else val -= next_val;
            } else {
                break;
            }
        }
        return val;
    }

    double parse_term() {
        double val = parse_factor();
        while (true) {
            skip_ws();
            if (pos < src.size() && (src[pos] == '*' || src[pos] == '/' || src[pos] == '%')) {
                char op = src[pos++];
                double next_val = parse_factor();
                if (op == '*') val *= next_val;
                else if (op == '/') {
                    if (std::abs(next_val) < 1e-15) throw std::runtime_error("Division by zero");
                    val /= next_val;
                } else {
                    if (std::abs(next_val) < 1e-15) throw std::runtime_error("Modulo by zero");
                    val = std::fmod(val, next_val);
                }
            } else {
                break;
            }
        }
        return val;
    }

    double parse_factor() {
        double val = parse_unary();
        skip_ws();
        if (pos < src.size() && src[pos] == '^') {
            ++pos;
            double exp = parse_factor();
            val = std::pow(val, exp);
        }
        return val;
    }

    double parse_unary() {
        skip_ws();
        if (pos < src.size() && src[pos] == '+') {
            ++pos;
            return parse_unary();
        }
        if (pos < src.size() && src[pos] == '-') {
            ++pos;
            return -parse_unary();
        }
        return parse_primary();
    }

    double parse_primary() {
        skip_ws();
        if (pos >= src.size()) throw std::runtime_error("Unexpected end of expression");

        if (src[pos] == '(') {
            ++pos;
            double v = parse_expression();
            skip_ws();
            if (pos >= src.size() || src[pos] != ')') throw std::runtime_error("Expected closing ')'");
            ++pos;
            return v;
        }

        // Functions: sqrt, sin, cos, log, abs, round
        if (std::isalpha(static_cast<unsigned char>(src[pos]))) {
            size_t start = pos;
            while (pos < src.size() && std::isalpha(static_cast<unsigned char>(src[pos]))) ++pos;
            std::string fn = src.substr(start, pos - start);
            skip_ws();
            if (pos >= src.size() || src[pos] != '(') throw std::runtime_error("Expected '(' after function " + fn);
            ++pos;
            double arg = parse_expression();
            skip_ws();
            if (pos >= src.size() || src[pos] != ')') throw std::runtime_error("Expected ')' after argument of " + fn);
            ++pos;

            if (fn == "sqrt") {
                if (arg < 0.0) throw std::runtime_error("Square root of negative number");
                return std::sqrt(arg);
            }
            if (fn == "sin") return std::sin(arg);
            if (fn == "cos") return std::cos(arg);
            if (fn == "log") {
                if (arg <= 0.0) throw std::runtime_error("Log of non-positive number");
                return std::log(arg);
            }
            if (fn == "abs") return std::abs(arg);
            if (fn == "round") return std::round(arg);

            throw std::runtime_error("Unsupported function: " + fn);
        }

        // Number
        size_t start = pos;
        bool has_dot = false;
        while (pos < src.size() && (std::isdigit(static_cast<unsigned char>(src[pos])) || src[pos] == '.')) {
            if (src[pos] == '.') {
                if (has_dot) break;
                has_dot = true;
            }
            ++pos;
        }
        if (start == pos) throw std::runtime_error("Expected number at position " + std::to_string(pos));
        return std::stod(src.substr(start, pos - start));
    }
};

vinox_status math_calc_handler(const vinox_tool_call_request* request, vinox_tool_call_result* result_out, char* pool_buf, size_t pool_buf_size, void*) {
    if (!request || !result_out) return VINOX_STATUS_INVALID_ARGUMENT;

    nlohmann::json args;
    try {
        args = nlohmann::json::parse(request->arguments_json ? request->arguments_json : "{}");
    } catch (...) {
        result_out->status_code = 400;
        result_out->error_message = "Malformed JSON arguments";
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::string expr = args.value("expression", "");
    if (expr.empty()) {
        result_out->status_code = 400;
        result_out->error_message = "Missing 'expression' argument";
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    try {
        MathParser parser(expr);
        double val = parser.parse_expression();

        nlohmann::json res;
        res["expression"] = expr;
        res["result"] = val;
        std::string out_str = res.dump();

        size_t off = 0;
        const char* c_res = copy_to_pool(out_str, pool_buf, pool_buf_size, off);
        if (!c_res) return VINOX_STATUS_RUNTIME_ERROR;

        result_out->status_code = 0;
        result_out->result_json = c_res;
        return VINOX_STATUS_OK;
    } catch (const std::exception& e) {
        result_out->status_code = 400;
        result_out->error_message = e.what();
        return VINOX_STATUS_INVALID_ARGUMENT;
    }
}

// -------------------------------------------------------------
// std_time Plugin Implementation (ISO-8601 with Timezone Offset)
// -------------------------------------------------------------
vinox_status time_query_handler(const vinox_tool_call_request* request, vinox_tool_call_result* result_out, char* pool_buf, size_t pool_buf_size, void*) {
    if (!request || !result_out) return VINOX_STATUS_INVALID_ARGUMENT;

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);

    std::tm tm_utc{}, tm_local{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &tt);
    localtime_s(&tm_local, &tt);
#else
    gmtime_r(&tt, &tm_utc);
    localtime_r(&tt, &tm_local);
#endif

    // Compute timezone offset minutes
    std::time_t t_utc = std::mktime(&tm_utc);
    std::time_t t_loc = std::mktime(&tm_local);
    int offset_minutes = static_cast<int>((t_loc - t_utc) / 60);

    // Format UTC string: YYYY-MM-DDTHH:MM:SSZ
    char utc_buf[64];
    std::strftime(utc_buf, sizeof(utc_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    // Format local string: YYYY-MM-DDTHH:MM:SS+HH:MM
    char loc_buf[64];
    int offset_h = std::abs(offset_minutes) / 60;
    int offset_m = std::abs(offset_minutes) % 60;
    char sign = offset_minutes >= 0 ? '+' : '-';
    char raw_loc[64];
    std::strftime(raw_loc, sizeof(raw_loc), "%Y-%m-%dT%H:%M:%S", &tm_local);
    snprintf(loc_buf, sizeof(loc_buf), "%s%c%02d:%02d", raw_loc, sign, offset_h, offset_m);

    nlohmann::json res;
    res["utc_time"] = utc_buf;
    res["local_time"] = loc_buf;
    res["timezone_offset_minutes"] = offset_minutes;
    res["unix_timestamp_seconds"] = static_cast<uint64_t>(tt);
    std::string out_str = res.dump();

    size_t off = 0;
    const char* c_res = copy_to_pool(out_str, pool_buf, pool_buf_size, off);
    if (!c_res) return VINOX_STATUS_RUNTIME_ERROR;

    result_out->status_code = 0;
    result_out->result_json = c_res;
    return VINOX_STATUS_OK;
}

// -------------------------------------------------------------
// std_retrieval Plugin Implementation (Dependency Injected Storage & Embedding)
// -------------------------------------------------------------
struct RetrievalPluginState {
    vinox_storage_engine* storage{nullptr};
    vinox_embedding_engine* embedding{nullptr};
};

vinox_status retrieval_search_handler(const vinox_tool_call_request* request, vinox_tool_call_result* result_out, char* pool_buf, size_t pool_buf_size, void* user_data) {
    auto* state = static_cast<RetrievalPluginState*>(user_data);
    if (!state || !state->storage || !request || !result_out) return VINOX_STATUS_INVALID_ARGUMENT;

    nlohmann::json args;
    try {
        args = nlohmann::json::parse(request->arguments_json ? request->arguments_json : "{}");
    } catch (...) {
        result_out->status_code = 400;
        result_out->error_message = "Malformed JSON arguments";
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::string query = args.value("query", "");
    float alpha = args.value("alpha", 0.5f);
    size_t limit = args.value("limit", 5);
    if (limit == 0) limit = 5;
    if (limit > 50) limit = 50;

    std::vector<float> query_vec;
    if (state->embedding) {
        size_t dim = 0;
        vinox_embedding_get_dim(state->embedding, &dim);
        if (dim > 0) {
            query_vec.resize(dim);
            size_t actual_dim = 0;
            vinox_embedding_generate(state->embedding, query.c_str(), query_vec.data(), query_vec.size(), &actual_dim);
        }
    }

    std::vector<vinox_search_result> raw_results(limit);
    for (auto& r : raw_results) r.struct_size = sizeof(r);
    size_t count = 0;

    vinox_status st = vinox_storage_search_hybrid(
        state->storage,
        query_vec.empty() ? nullptr : query_vec.data(),
        query_vec.size(),
        query.c_str(),
        alpha,
        limit,
        raw_results.data(),
        &count
    );

    if (st != VINOX_STATUS_OK) {
        result_out->status_code = 500;
        result_out->error_message = vinox_storage_last_error();
        return st;
    }

    nlohmann::json matches = nlohmann::json::array();
    for (size_t i = 0; i < count; ++i) {
        nlohmann::json m;
        m["message_id"] = raw_results[i].message_id ? raw_results[i].message_id : "";
        m["bm25_score"] = raw_results[i].bm25_score;
        m["vector_score"] = raw_results[i].vector_score;
        m["hybrid_score"] = raw_results[i].hybrid_score;
        matches.push_back(m);
    }

    nlohmann::json res;
    res["query"] = query;
    res["matches"] = matches;
    res["count"] = count;
    std::string out_str = res.dump();

    size_t off = 0;
    const char* c_res = copy_to_pool(out_str, pool_buf, pool_buf_size, off);
    if (!c_res) return VINOX_STATUS_RUNTIME_ERROR;

    result_out->status_code = 0;
    result_out->result_json = c_res;
    return VINOX_STATUS_OK;
}

vinox_status retrieval_ingest_handler(const vinox_tool_call_request* request, vinox_tool_call_result* result_out, char* pool_buf, size_t pool_buf_size, void* user_data) {
    auto* state = static_cast<RetrievalPluginState*>(user_data);
    if (!state || !state->storage || !request || !result_out) return VINOX_STATUS_INVALID_ARGUMENT;

    nlohmann::json args;
    try {
        args = nlohmann::json::parse(request->arguments_json ? request->arguments_json : "{}");
    } catch (...) {
        result_out->status_code = 400;
        result_out->error_message = "Malformed JSON arguments";
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::string title = args.value("title", "Untitled Document");
    std::string content = args.value("content", "");
    if (content.empty()) {
        result_out->status_code = 400;
        result_out->error_message = "Content cannot be empty";
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    char doc_id_buf[128] = {0};
    vinox_status st = vinox_storage_document_ingest(state->storage, title.c_str(), content.c_str(), doc_id_buf, sizeof(doc_id_buf));
    if (st != VINOX_STATUS_OK) {
        result_out->status_code = 500;
        result_out->error_message = vinox_storage_last_error();
        return st;
    }

    nlohmann::json res;
    res["document_id"] = doc_id_buf;
    res["title"] = title;
    res["bytes"] = content.size();
    res["status"] = "ingested";
    std::string out_str = res.dump();

    size_t off = 0;
    const char* c_res = copy_to_pool(out_str, pool_buf, pool_buf_size, off);
    if (!c_res) return VINOX_STATUS_RUNTIME_ERROR;

    result_out->status_code = 0;
    result_out->result_json = c_res;
    return VINOX_STATUS_OK;
}

} // namespace

extern "C" {

const char* vinox_plugins_last_error(void) {
    return g_plugins_last_error.c_str();
}

vinox_status vinox_plugin_register(vinox_tool_registry* registry, vinox_tool_plugin* plugin) {
    if (!registry || !plugin) {
        set_plugins_last_error("registry and plugin cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (plugin->struct_size < VINOX_TOOL_PLUGIN_MIN_SIZE) {
        set_plugins_last_error("plugin struct_size is too small");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    if (plugin->abi_version != VINOX_PLUGIN_ABI_VERSION_1) {
        set_plugins_last_error("Unsupported plugin ABI version");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    if (plugin->register_tools) {
        return plugin->register_tools(plugin, registry);
    }

    return VINOX_STATUS_OK;
}

void vinox_plugin_destroy(vinox_tool_plugin* plugin) {
    if (plugin) {
        if (plugin->destroy) {
            plugin->destroy(plugin);
        }
        delete plugin;
    }
}

// -------------------------------------------------------------
// std_fs Plugin Registration
// -------------------------------------------------------------
vinox_status vinox_plugin_std_fs_create(const char* workspace_root, vinox_tool_plugin** plugin_out) {
    if (!workspace_root || !plugin_out) {
        set_plugins_last_error("workspace_root and plugin_out cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::filesystem::path root_p(workspace_root);
    std::filesystem::path canon_root = std::filesystem::weakly_canonical(root_p);

    auto* state = new FsPluginState{canon_root};
    auto* plug = new vinox_tool_plugin();
    plug->struct_size = sizeof(vinox_tool_plugin);
    plug->abi_version = VINOX_PLUGIN_ABI_VERSION_1;
    plug->name = "std_fs";
    plug->version = "1.0.0";
    plug->description = "Standard Sandboxed Filesystem Tools (Bounded to workspace root)";
    plug->declared_scopes = VINOX_PLUGIN_SCOPE_FS_READ | VINOX_PLUGIN_SCOPE_FS_WRITE;
    plug->user_data = state;

    plug->init = [](vinox_tool_plugin*, void*) -> vinox_status { return VINOX_STATUS_OK; };
    plug->destroy = [](vinox_tool_plugin* self) {
        if (self && self->user_data) {
            delete static_cast<FsPluginState*>(self->user_data);
            self->user_data = nullptr;
        }
    };
    plug->register_tools = [](vinox_tool_plugin* self, vinox_tool_registry* reg) -> vinox_status {
        vinox_tool_definition def_read{};
        def_read.struct_size = sizeof(def_read);
        def_read.name = "fs.read";
        def_read.description = "Read file content within workspace sandbox";
        def_read.parameters_json_schema = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"],\"additionalProperties\":false}";
        def_read.security_class = VINOX_SECURITY_CLASS_READ_ONLY;
        vinox_tool_registry_register_tool(reg, &def_read);
        vinox_tool_registry_register_handler(reg, "fs.read", fs_read_handler, self->user_data);

        vinox_tool_definition def_write{};
        def_write.struct_size = sizeof(def_write);
        def_write.name = "fs.write";
        def_write.description = "Write file content within workspace sandbox";
        def_write.parameters_json_schema = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"],\"additionalProperties\":false}";
        def_write.security_class = VINOX_SECURITY_CLASS_LOCAL_WRITE;
        vinox_tool_registry_register_tool(reg, &def_write);
        vinox_tool_registry_register_handler(reg, "fs.write", fs_write_handler, self->user_data);

        vinox_tool_definition def_list{};
        def_list.struct_size = sizeof(def_list);
        def_list.name = "fs.list";
        def_list.description = "List directory entries within workspace sandbox";
        def_list.parameters_json_schema = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"additionalProperties\":false}";
        def_list.security_class = VINOX_SECURITY_CLASS_READ_ONLY;
        vinox_tool_registry_register_tool(reg, &def_list);
        vinox_tool_registry_register_handler(reg, "fs.list", fs_list_handler, self->user_data);

        return VINOX_STATUS_OK;
    };

    *plugin_out = plug;
    return VINOX_STATUS_OK;
}

// -------------------------------------------------------------
// std_math Plugin Registration
// -------------------------------------------------------------
vinox_status vinox_plugin_std_math_create(vinox_tool_plugin** plugin_out) {
    if (!plugin_out) return VINOX_STATUS_INVALID_ARGUMENT;

    auto* plug = new vinox_tool_plugin();
    plug->struct_size = sizeof(vinox_tool_plugin);
    plug->abi_version = VINOX_PLUGIN_ABI_VERSION_1;
    plug->name = "std_math";
    plug->version = "1.0.0";
    plug->description = "Standard Deterministic Math Calculator (Bounded arithmetic, power and trigonometry grammar)";
    plug->declared_scopes = VINOX_PLUGIN_SCOPE_MATH;
    plug->user_data = nullptr;

    plug->init = [](vinox_tool_plugin*, void*) -> vinox_status { return VINOX_STATUS_OK; };
    plug->destroy = [](vinox_tool_plugin*) {};
    plug->register_tools = [](vinox_tool_plugin*, vinox_tool_registry* reg) -> vinox_status {
        vinox_tool_definition def_calc{};
        def_calc.struct_size = sizeof(def_calc);
        def_calc.name = "math.calculate";
        def_calc.description = "Evaluate a mathematical expression using a bounded, safe grammar (+, -, *, /, %, ^, sqrt, sin, cos, log, abs, round)";
        def_calc.parameters_json_schema = "{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"}},\"required\":[\"expression\"],\"additionalProperties\":false}";
        def_calc.security_class = VINOX_SECURITY_CLASS_READ_ONLY;
        vinox_tool_registry_register_tool(reg, &def_calc);
        vinox_tool_registry_register_handler(reg, "math.calculate", math_calc_handler, nullptr);
        return VINOX_STATUS_OK;
    };

    *plugin_out = plug;
    return VINOX_STATUS_OK;
}

// -------------------------------------------------------------
// std_time Plugin Registration
// -------------------------------------------------------------
vinox_status vinox_plugin_std_time_create(vinox_tool_plugin** plugin_out) {
    if (!plugin_out) return VINOX_STATUS_INVALID_ARGUMENT;

    auto* plug = new vinox_tool_plugin();
    plug->struct_size = sizeof(vinox_tool_plugin);
    plug->abi_version = VINOX_PLUGIN_ABI_VERSION_1;
    plug->name = "std_time";
    plug->version = "1.0.0";
    plug->description = "Standard System Time & Date Service (UTC and Local ISO-8601 with Timezone Offset)";
    plug->declared_scopes = VINOX_PLUGIN_SCOPE_SYSTEM_TIME;
    plug->user_data = nullptr;

    plug->init = [](vinox_tool_plugin*, void*) -> vinox_status { return VINOX_STATUS_OK; };
    plug->destroy = [](vinox_tool_plugin*) {};
    plug->register_tools = [](vinox_tool_plugin*, vinox_tool_registry* reg) -> vinox_status {
        vinox_tool_definition def_time{};
        def_time.struct_size = sizeof(def_time);
        def_time.name = "system.time";
        def_time.description = "Query current system time (UTC & local ISO-8601 with timezone offset and unix timestamp)";
        def_time.parameters_json_schema = "{\"type\":\"object\",\"additionalProperties\":false}";
        def_time.security_class = VINOX_SECURITY_CLASS_READ_ONLY;
        vinox_tool_registry_register_tool(reg, &def_time);
        vinox_tool_registry_register_handler(reg, "system.time", time_query_handler, nullptr);
        return VINOX_STATUS_OK;
    };

    *plugin_out = plug;
    return VINOX_STATUS_OK;
}

// -------------------------------------------------------------
// std_retrieval Plugin Registration
// -------------------------------------------------------------
vinox_status vinox_plugin_std_retrieval_create(vinox_storage_engine* storage, vinox_embedding_engine* embedding, vinox_tool_plugin** plugin_out) {
    if (!plugin_out) {
        set_plugins_last_error("plugin_out cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    auto* state = new RetrievalPluginState{storage, embedding};
    auto* plug = new vinox_tool_plugin();
    plug->struct_size = sizeof(vinox_tool_plugin);
    plug->abi_version = VINOX_PLUGIN_ABI_VERSION_1;
    plug->name = "std_retrieval";
    plug->version = "1.0.0";
    plug->description = "VINOX Hybrid Retrieval & Document Ingestion Plugin";
    plug->declared_scopes = VINOX_PLUGIN_SCOPE_RETRIEVAL_SEARCH | VINOX_PLUGIN_SCOPE_RETRIEVAL_MUTATE;
    plug->user_data = state;

    plug->init = [](vinox_tool_plugin* self, void* context) -> vinox_status {
        if (context && self && self->user_data) {
            auto* state_ptr = static_cast<RetrievalPluginState*>(self->user_data);
            auto* p = static_cast<void**>(context);
            if (p[0]) state_ptr->storage = static_cast<vinox_storage_engine*>(p[0]);
            if (p[1]) state_ptr->embedding = static_cast<vinox_embedding_engine*>(p[1]);
        }
        return VINOX_STATUS_OK;
    };
    plug->destroy = [](vinox_tool_plugin* self) {
        if (self && self->user_data) {
            delete static_cast<RetrievalPluginState*>(self->user_data);
            self->user_data = nullptr;
        }
    };
    plug->register_tools = [](vinox_tool_plugin* self, vinox_tool_registry* reg) -> vinox_status {
        // Query group (READ_ONLY)
        vinox_tool_definition def_search{};
        def_search.struct_size = sizeof(def_search);
        def_search.name = "vinox.search";
        def_search.description = "VINOX Hybrid Retrieval (BM25 + sqlite-vec Vector Search)";
        def_search.parameters_json_schema = "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"alpha\":{\"type\":\"number\"},\"limit\":{\"type\":\"integer\"}},\"required\":[\"query\"],\"additionalProperties\":false}";
        def_search.security_class = VINOX_SECURITY_CLASS_READ_ONLY;
        vinox_tool_registry_register_tool(reg, &def_search);
        vinox_tool_registry_register_handler(reg, "vinox.search", retrieval_search_handler, self->user_data);

        // Mutation group (LOCAL_WRITE)
        vinox_tool_definition def_ingest{};
        def_ingest.struct_size = sizeof(def_ingest);
        def_ingest.name = "vinox.document_ingest";
        def_ingest.description = "Ingest text document into VINOX storage";
        def_ingest.parameters_json_schema = "{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"title\",\"content\"],\"additionalProperties\":false}";
        def_ingest.security_class = VINOX_SECURITY_CLASS_LOCAL_WRITE;
        vinox_tool_registry_register_tool(reg, &def_ingest);
        vinox_tool_registry_register_handler(reg, "vinox.document_ingest", retrieval_ingest_handler, self->user_data);

        return VINOX_STATUS_OK;
    };

    *plugin_out = plug;
    return VINOX_STATUS_OK;
}

// -------------------------------------------------------------
// Dynamic Plugin Loading (Trust & Allowlist + Fixed Entry Point)
// -------------------------------------------------------------
vinox_status vinox_plugin_load_dynamic(const char* library_path, const char* expected_sha256, vinox_tool_plugin** plugin_out) {
    if (!library_path || !plugin_out) {
        set_plugins_last_error("library_path and plugin_out cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (!std::filesystem::exists(library_path)) {
        set_plugins_last_error(std::string("Plugin library not found: ") + library_path);
        return VINOX_STATUS_NOT_FOUND;
    }

    // Hash check if expected_sha256 provided
    if (expected_sha256 && expected_sha256[0] != '\0') {
        std::string actual_sha = calculate_file_sha256(library_path);
        std::string exp_lower = expected_sha256;
        std::transform(exp_lower.begin(), exp_lower.end(), exp_lower.begin(), [](unsigned char c) { return std::tolower(c); });
        std::string act_lower = actual_sha;
        std::transform(act_lower.begin(), act_lower.end(), act_lower.begin(), [](unsigned char c) { return std::tolower(c); });

        if (exp_lower != act_lower) {
            set_plugins_last_error("Plugin SHA256 mismatch! Expected: " + exp_lower + ", Actual: " + act_lower);
            return VINOX_STATUS_PERMISSION_DENIED;
        }
    }

#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::string full_abs_path = std::filesystem::absolute(library_path).string();
    HMODULE hMod = LoadLibraryExA(full_abs_path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hMod) {
        hMod = LoadLibraryA(library_path);
    }
    if (!hMod) {
        set_plugins_last_error("Failed to load plugin DLL: " + std::to_string(GetLastError()));
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    auto entry_fn = reinterpret_cast<vinox_plugin_entry_fn>(GetProcAddress(hMod, VINOX_PLUGIN_ENTRY_SYMBOL));
    if (!entry_fn) {
        FreeLibrary(hMod);
        set_plugins_last_error("Plugin does not export required entry point symbol: " VINOX_PLUGIN_ENTRY_SYMBOL);
        return VINOX_STATUS_NOT_SUPPORTED;
    }
#else
    void* hMod = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (!hMod) {
        set_plugins_last_error(std::string("Failed to load plugin library: ") + dlerror());
        return VINOX_STATUS_RUNTIME_ERROR;
    }

    auto entry_fn = reinterpret_cast<vinox_plugin_entry_fn>(dlsym(hMod, VINOX_PLUGIN_ENTRY_SYMBOL));
    if (!entry_fn) {
        dlclose(hMod);
        set_plugins_last_error("Plugin does not export required entry point symbol: " VINOX_PLUGIN_ENTRY_SYMBOL);
        return VINOX_STATUS_NOT_SUPPORTED;
    }
#endif

    auto* plug = new vinox_tool_plugin();
    std::memset(plug, 0, sizeof(vinox_tool_plugin));
    plug->struct_size = sizeof(vinox_tool_plugin);
    plug->abi_version = VINOX_PLUGIN_ABI_VERSION_1;

    vinox_status st = entry_fn(plug);
    if (st != VINOX_STATUS_OK) {
#ifdef _WIN32
        FreeLibrary(hMod);
#else
        dlclose(hMod);
#endif
        delete plug;
        set_plugins_last_error("Plugin entry point execution failed");
        return st;
    }

    if (plug->struct_size < VINOX_TOOL_PLUGIN_MIN_SIZE || plug->abi_version != VINOX_PLUGIN_ABI_VERSION_1) {
#ifdef _WIN32
        FreeLibrary(hMod);
#else
        dlclose(hMod);
#endif
        delete plug;
        set_plugins_last_error("Plugin ABI check failed");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    *plugin_out = plug;
    return VINOX_STATUS_OK;
}

} // extern "C"
