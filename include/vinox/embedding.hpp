#ifndef VINOX_EMBEDDING_HPP
#define VINOX_EMBEDDING_HPP

#include <memory>
#include <string>
#include <vector>
#include "vinox/embedding.h"

namespace vinox {
namespace embedding {

class EmbeddingEngine {
public:
    EmbeddingEngine() : engine_(nullptr) {}

    ~EmbeddingEngine() {
        reset();
    }

    EmbeddingEngine(const EmbeddingEngine&) = delete;
    EmbeddingEngine& operator=(const EmbeddingEngine&) = delete;

    EmbeddingEngine(EmbeddingEngine&& other) noexcept : engine_(other.engine_) {
        other.engine_ = nullptr;
    }

    EmbeddingEngine& operator=(EmbeddingEngine&& other) noexcept {
        if (this != &other) {
            reset();
            engine_ = other.engine_;
            other.engine_ = nullptr;
        }
        return *this;
    }

    void reset() {
        if (engine_) {
            vinox_embedding_engine_destroy(engine_);
            engine_ = nullptr;
        }
    }

    vinox_embedding_engine* get() const { return engine_; }
    bool is_valid() const { return engine_ != nullptr; }

    vinox_status load(
        const std::string& model_path,
        const std::string& device = "CPU",
        vinox_embedding_pooling_mode pooling = VINOX_EMBEDDING_POOLING_AUTO,
        vinox_embedding_normalization norm = VINOX_EMBEDDING_NORM_AUTO,
        bool enable_mmap = true,
        bool enable_cache = true,
        const std::string& cache_dir = ""
    ) {
        reset();
        vinox_embedding_options opts{};
        opts.struct_size = sizeof(opts);
        opts.model_path = model_path.c_str();
        opts.device = device.c_str();
        opts.pooling_mode = static_cast<uint32_t>(pooling);
        opts.normalization = static_cast<uint32_t>(norm);
        opts.enable_mmap = enable_mmap ? 1 : 0;
        opts.enable_cache = enable_cache ? 1 : 0;
        opts.cache_dir = cache_dir.empty() ? nullptr : cache_dir.c_str();

        return vinox_embedding_engine_create(&opts, &engine_);
    }

    size_t dimension() const {
        if (!engine_) return 0;
        size_t dim = 0;
        vinox_embedding_get_dim(engine_, &dim);
        return dim;
    }

    vinox_status generate(const std::string& text, std::vector<float>& out_vec) const {
        if (!engine_) return VINOX_STATUS_INVALID_ARGUMENT;
        size_t dim = dimension();
        if (dim == 0) return VINOX_STATUS_RUNTIME_ERROR;
        out_vec.resize(dim);
        size_t actual_dim = 0;
        vinox_status st = vinox_embedding_generate(engine_, text.c_str(), out_vec.data(), out_vec.size(), &actual_dim);
        if (st == VINOX_STATUS_OK && actual_dim != dim) {
            out_vec.resize(actual_dim);
        }
        return st;
    }

    vinox_status generate_batch(const std::vector<std::string>& texts, std::vector<std::vector<float>>& out_vecs) const {
        if (!engine_ || texts.empty()) return VINOX_STATUS_INVALID_ARGUMENT;
        size_t dim = dimension();
        if (dim == 0) return VINOX_STATUS_RUNTIME_ERROR;

        std::vector<const char*> c_texts(texts.size());
        for (size_t i = 0; i < texts.size(); ++i) {
            c_texts[i] = texts[i].c_str();
        }

        std::vector<float> flat_out(texts.size() * dim);
        size_t actual_dim = 0;
        vinox_status st = vinox_embedding_generate_batch(
            engine_,
            c_texts.data(),
            c_texts.size(),
            flat_out.data(),
            dim,
            &actual_dim
        );

        if (st == VINOX_STATUS_OK) {
            out_vecs.resize(texts.size());
            for (size_t i = 0; i < texts.size(); ++i) {
                out_vecs[i].assign(flat_out.begin() + (i * actual_dim), flat_out.begin() + ((i + 1) * actual_dim));
            }
        }
        return st;
    }

private:
    vinox_embedding_engine* engine_;
};

} // namespace embedding
} // namespace vinox

#endif // VINOX_EMBEDDING_HPP
