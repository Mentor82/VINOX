#include "vinox/embedding.h"
#include "vinox/embedding.hpp"
#include "vinox/logging.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "openvino/openvino.hpp"
#include "openvino/genai/tokenizer.hpp"

namespace {

thread_local std::string g_embedding_last_error;

void set_embedding_last_error(const std::string& err) {
    g_embedding_last_error = err;
}

// Compact SHA-256 implementation for provenance hashing
struct Sha256Ctx {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
};

inline uint32_t rrot(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void sha256_trans(Sha256Ctx* ctx, const uint8_t data[64]) {
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
        uint32_t s0 = rrot(m[i - 15], 7) ^ rrot(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rrot(m[i - 2], 17) ^ rrot(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rrot(e, 6) ^ rrot(e, 11) ^ rrot(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[i] + m[i];
        uint32_t S0 = rrot(a, 2) ^ rrot(a, 13) ^ rrot(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_setup(Sha256Ctx* ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

void sha256_feed(Sha256Ctx* ctx, const uint8_t* data, size_t len) {
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
            sha256_trans(ctx, ctx->buffer);
            idx = 0;
        }
    }
}

std::string sha256_finish(Sha256Ctx* ctx) {
    uint8_t pad[64] = {0x80};
    uint8_t len_bytes[8];
    uint64_t bits = ctx->count * 8;
    for (int i = 0; i < 8; ++i) {
        len_bytes[7 - i] = static_cast<uint8_t>(bits >> (i * 8));
    }
    size_t pad_len = (ctx->count % 64 < 56) ? (56 - (ctx->count % 64)) : (120 - (ctx->count % 64));
    sha256_feed(ctx, pad, pad_len);
    sha256_feed(ctx, len_bytes, 8);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) {
        ss << std::setw(8) << ctx->state[i];
    }
    return ss.str();
}

std::string file_sha256_quick(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"; // Empty hash
    Sha256Ctx ctx;
    sha256_setup(&ctx);
    std::vector<char> buf(262144);
    while (f.read(buf.data(), buf.size()) || f.gcount() > 0) {
        sha256_feed(&ctx, reinterpret_cast<const uint8_t*>(buf.data()), static_cast<size_t>(f.gcount()));
    }
    return sha256_finish(&ctx);
}

} // namespace

struct vinox_embedding_engine {
    std::mutex mutex;
    std::string model_id;
    std::string device;
    size_t dimension{0};
    uint32_t pooling_mode{VINOX_EMBEDDING_POOLING_MEAN};
    uint32_t normalization{VINOX_EMBEDDING_NORM_L2};
    std::string space_id;

    ov::Core core;
    ov::CompiledModel compiled_model;
    ov::InferRequest infer_request;
    std::unique_ptr<ov::genai::Tokenizer> tokenizer;
    bool output_is_rank3{true}; // true if shape is [batch, seq, dim], false if [batch, dim]
};

extern "C" {

const char* vinox_embedding_last_error(void) {
    return g_embedding_last_error.c_str();
}

vinox_status vinox_embedding_engine_create(
    const vinox_embedding_options* options,
    vinox_embedding_engine** engine_out
) {
    if (!options || !engine_out) {
        set_embedding_last_error("options and engine_out cannot be null");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (options->struct_size < VINOX_EMBEDDING_OPTIONS_MIN_SIZE) {
        set_embedding_last_error("options->struct_size is too small");
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    if (!options->model_path || options->model_path[0] == '\0') {
        set_embedding_last_error("model_path cannot be empty");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    std::string model_dir = options->model_path;
    std::string device = (options->device && options->device[0] != '\0') ? options->device : "CPU";

    try {
        auto engine = std::make_unique<vinox_embedding_engine>();
        engine->model_id = std::filesystem::path(model_dir).filename().string();
        engine->device = device;

        // Configure properties on isolated ov::Core instance
        if (options->enable_mmap) {
            engine->core.set_property(ov::enable_mmap(true));
        }
        if (options->enable_cache && options->cache_dir && options->cache_dir[0] != '\0') {
            engine->core.set_property(ov::cache_dir(options->cache_dir));
        }

        // Initialize tokenizer
        engine->tokenizer = std::make_unique<ov::genai::Tokenizer>(model_dir);

        // Find and read OpenVINO model XML
        std::filesystem::path xml_path = std::filesystem::path(model_dir) / "openvino_model.xml";
        if (!std::filesystem::exists(xml_path)) {
            set_embedding_last_error("openvino_model.xml not found in directory: " + model_dir);
            return VINOX_STATUS_NOT_FOUND;
        }

        auto model = engine->core.read_model(xml_path.string());
        if (model->outputs().empty()) {
            set_embedding_last_error("Model has no outputs");
            return VINOX_STATUS_RUNTIME_ERROR;
        }

        // Determine output shape & dimension dynamically from model
        auto out_pshape = model->output(0).get_partial_shape();
        size_t rank = out_pshape.rank().is_static() ? out_pshape.rank().get_length() : 3;
        size_t detected_dim = 0;

        if (rank == 3) {
            engine->output_is_rank3 = true;
            if (out_pshape[2].is_static()) {
                detected_dim = static_cast<size_t>(out_pshape[2].get_length());
            }
        } else if (rank == 2) {
            engine->output_is_rank3 = false;
            if (out_pshape[1].is_static()) {
                detected_dim = static_cast<size_t>(out_pshape[1].get_length());
            }
        }

        // AUTO Strategy Resolution with Fail-Closed Rule
        uint32_t resolved_pooling = options->pooling_mode;
        if (resolved_pooling == VINOX_EMBEDDING_POOLING_AUTO) {
            if (!engine->output_is_rank3) {
                resolved_pooling = VINOX_EMBEDDING_POOLING_MODEL_OUTPUT;
            } else {
                // Inspect config.json for architecture
                std::filesystem::path cfg_p = std::filesystem::path(model_dir) / "config.json";
                std::string arch_type;
                if (std::filesystem::exists(cfg_p)) {
                    std::ifstream cf(cfg_p);
                    try {
                        auto cj = nlohmann::json::parse(cf);
                        if (cj.contains("model_type") && cj["model_type"].is_string()) {
                            arch_type = cj["model_type"].get<std::string>();
                        } else if (cj.contains("architectures") && cj["architectures"].is_array() && !cj["architectures"].empty()) {
                            arch_type = cj["architectures"][0].get<std::string>();
                        }
                        if (detected_dim == 0 && cj.contains("hidden_size") && cj["hidden_size"].is_number_integer()) {
                            detected_dim = cj["hidden_size"].get<size_t>();
                        }
                    } catch (...) {}
                }

                std::string lower_arch = arch_type;
                std::transform(lower_arch.begin(), lower_arch.end(), lower_arch.begin(), [](unsigned char c){ return std::tolower(c); });

                if (lower_arch.find("qwen") != std::string::npos ||
                    lower_arch.find("bge") != std::string::npos ||
                    lower_arch.find("llama") != std::string::npos ||
                    lower_arch.find("mistral") != std::string::npos ||
                    lower_arch.find("bert") != std::string::npos) {
                    resolved_pooling = VINOX_EMBEDDING_POOLING_MEAN;
                } else {
                    set_embedding_last_error("Fail-closed: Cannot automatically infer pooling strategy for architecture '" + arch_type + "'. Explicit pooling mode required.");
                    return VINOX_STATUS_MODEL_PROTOCOL_UNSUPPORTED;
                }
            }
        }

        uint32_t resolved_norm = options->normalization;
        if (resolved_norm == VINOX_EMBEDDING_NORM_AUTO) {
            resolved_norm = VINOX_EMBEDDING_NORM_L2;
        }

        // Fallback check on detected dim
        if (detected_dim == 0) {
            detected_dim = 1024; // Standard fallback if completely dynamic
        }

        engine->dimension = detected_dim;
        engine->pooling_mode = resolved_pooling;
        engine->normalization = resolved_norm;

        // Compile model on target device
        engine->compiled_model = engine->core.compile_model(model, device);
        engine->infer_request = engine->compiled_model.create_infer_request();

        // Calculate Provenance Hash: SHA-256(weights_hash + tokenizer_hash + pooling + norm + dim)
        std::filesystem::path bin_path = std::filesystem::path(model_dir) / "openvino_model.bin";
        std::string weights_hash = std::filesystem::exists(bin_path) ? file_sha256_quick(bin_path.string()) : file_sha256_quick(xml_path.string());
        std::filesystem::path tok_path = std::filesystem::path(model_dir) / "tokenizer.json";
        std::string tok_hash = std::filesystem::exists(tok_path) ? file_sha256_quick(tok_path.string()) : "tok_default";

        Sha256Ctx prov_ctx;
        sha256_setup(&prov_ctx);
        sha256_feed(&prov_ctx, reinterpret_cast<const uint8_t*>(weights_hash.data()), weights_hash.size());
        sha256_feed(&prov_ctx, reinterpret_cast<const uint8_t*>(tok_hash.data()), tok_hash.size());
        std::string meta_str = std::to_string(resolved_pooling) + ":" + std::to_string(resolved_norm) + ":" + std::to_string(detected_dim);
        sha256_feed(&prov_ctx, reinterpret_cast<const uint8_t*>(meta_str.data()), meta_str.size());
        engine->space_id = sha256_finish(&prov_ctx);

        *engine_out = engine.release();
        return VINOX_STATUS_OK;
    } catch (const std::exception& e) {
        set_embedding_last_error(std::string("OpenVINO embedding initialization failed: ") + e.what());
        return VINOX_STATUS_RUNTIME_ERROR;
    }
}

vinox_status vinox_embedding_engine_destroy(vinox_embedding_engine* engine) {
    if (engine) {
        delete engine;
    }
    return VINOX_STATUS_OK;
}

vinox_status vinox_embedding_generate_batch(
    vinox_embedding_engine* engine,
    const char* const* texts,
    size_t count,
    float* vectors_out,
    size_t vector_cap_per_item,
    size_t* dim_out
) {
    if (!engine || !texts || count == 0 || !vectors_out) {
        set_embedding_last_error("Invalid arguments for vinox_embedding_generate_batch");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (vector_cap_per_item < engine->dimension) {
        set_embedding_last_error("vector_cap_per_item is smaller than embedding dimension (" + std::to_string(engine->dimension) + ")");
        return VINOX_STATUS_INVALID_ARGUMENT;
    }

    if (dim_out) *dim_out = engine->dimension;

    std::lock_guard<std::mutex> lock(engine->mutex);

    try {
        for (size_t item = 0; item < count; ++item) {
            const char* txt = texts[item] ? texts[item] : "";
            auto tokenized = engine->tokenizer->encode(txt);

            engine->infer_request.set_tensor("input_ids", tokenized.input_ids);
            engine->infer_request.set_tensor("attention_mask", tokenized.attention_mask);
            engine->infer_request.infer();

            auto out_tensor = engine->infer_request.get_output_tensor(0);
            auto out_shape = out_tensor.get_shape();

            float* item_out = vectors_out + (item * vector_cap_per_item);

            if (engine->output_is_rank3 && out_shape.size() >= 3) {
                size_t seq_len = out_shape[1];
                size_t hidden_dim = out_shape[2];
                const float* hidden_data = out_tensor.data<const float>();
                const int64_t* mask_data = tokenized.attention_mask.data<const int64_t>();

                std::vector<float> pooled(hidden_dim, 0.0f);

                if (engine->pooling_mode == VINOX_EMBEDDING_POOLING_CLS) {
                    // CLS: first token hidden state
                    std::memcpy(pooled.data(), hidden_data, hidden_dim * sizeof(float));
                } else if (engine->pooling_mode == VINOX_EMBEDDING_POOLING_LAST_TOKEN) {
                    // Last non-padding token
                    size_t last_idx = 0;
                    for (size_t s = 0; s < seq_len; ++s) {
                        if (mask_data[s] != 0) last_idx = s;
                    }
                    std::memcpy(pooled.data(), hidden_data + (last_idx * hidden_dim), hidden_dim * sizeof(float));
                } else {
                    // MEAN Pooling (default)
                    float mask_sum = 0.0f;
                    for (size_t s = 0; s < seq_len; ++s) {
                        float m_val = static_cast<float>(mask_data[s]);
                        mask_sum += m_val;
                        for (size_t d = 0; d < hidden_dim; ++d) {
                            pooled[d] += hidden_data[s * hidden_dim + d] * m_val;
                        }
                    }
                    if (mask_sum > 0.0f) {
                        for (size_t d = 0; d < hidden_dim; ++d) pooled[d] /= mask_sum;
                    }
                }

                // Normalization
                if (engine->normalization == VINOX_EMBEDDING_NORM_L2) {
                    double norm = 0.0;
                    for (float v : pooled) norm += static_cast<double>(v) * static_cast<double>(v);
                    norm = std::sqrt(norm);
                    if (norm > 1e-12) {
                        float inv = static_cast<float>(1.0 / norm);
                        for (float& v : pooled) v *= inv;
                    }
                }

                std::memcpy(item_out, pooled.data(), std::min(hidden_dim, vector_cap_per_item) * sizeof(float));
            } else {
                // Direct model 2D output [batch, dim]
                const float* out_data = out_tensor.data<const float>();
                size_t dim = out_shape.back();
                std::memcpy(item_out, out_data, std::min(dim, vector_cap_per_item) * sizeof(float));

                if (engine->normalization == VINOX_EMBEDDING_NORM_L2) {
                    double norm = 0.0;
                    for (size_t d = 0; d < dim; ++d) norm += static_cast<double>(item_out[d]) * static_cast<double>(item_out[d]);
                    norm = std::sqrt(norm);
                    if (norm > 1e-12) {
                        float inv = static_cast<float>(1.0 / norm);
                        for (size_t d = 0; d < dim; ++d) item_out[d] *= inv;
                    }
                }
            }
        }
        return VINOX_STATUS_OK;
    } catch (const std::exception& e) {
        set_embedding_last_error(std::string("Embedding batch generation failed: ") + e.what());
        return VINOX_STATUS_RUNTIME_ERROR;
    }
}

vinox_status vinox_embedding_generate(
    vinox_embedding_engine* engine,
    const char* text,
    float* vector_out,
    size_t vector_cap,
    size_t* dim_out
) {
    // Single execution delegates directly to Batch-First core with count = 1
    const char* texts[1] = { text };
    return vinox_embedding_generate_batch(engine, texts, 1, vector_out, vector_cap, dim_out);
}

vinox_status vinox_embedding_get_dim(
    const vinox_embedding_engine* engine,
    size_t* dim_out
) {
    if (!engine || !dim_out) return VINOX_STATUS_INVALID_ARGUMENT;
    *dim_out = engine->dimension;
    return VINOX_STATUS_OK;
}

vinox_status vinox_embedding_get_info(
    const vinox_embedding_engine* engine,
    vinox_embedding_info* info_out,
    char* pool_buf,
    size_t pool_buf_size
) {
    if (!engine || !info_out || !pool_buf || pool_buf_size == 0) return VINOX_STATUS_INVALID_ARGUMENT;
    if (info_out->struct_size < VINOX_EMBEDDING_INFO_MIN_SIZE) return VINOX_STATUS_INCOMPATIBLE_ABI;

    size_t offset = 0;
    auto copy_str = [&](const std::string& str) -> const char* {
        if (offset + str.length() + 1 > pool_buf_size) return nullptr;
        char* dst = pool_buf + offset;
        std::memcpy(dst, str.c_str(), str.length());
        dst[str.length()] = '\0';
        offset += str.length() + 1;
        return dst;
    };

    info_out->backend_id = copy_str("openvino");
    info_out->model_id = copy_str(engine->model_id);
    info_out->device = copy_str(engine->device);
    info_out->dimension = engine->dimension;
    info_out->pooling_mode = engine->pooling_mode;
    info_out->normalization = engine->normalization;
    info_out->space_id = copy_str(engine->space_id);

    return VINOX_STATUS_OK;
}

} // extern "C"
