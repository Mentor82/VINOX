#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace vinox::storage {

/**
 * @brief L2 Normalization of vector in-place.
 */
void l2_normalize(std::vector<float>& vec) {
    if (vec.empty()) return;
    double sum = 0.0;
    for (float val : vec) {
        sum += static_cast<double>(val) * static_cast<double>(val);
    }
    double norm = std::sqrt(sum);
    if (norm > 1e-12) {
        float inv_norm = static_cast<float>(1.0 / norm);
        for (float& val : vec) {
            val *= inv_norm;
        }
    }
}

/**
 * @brief Cosine similarity between two vectors (assumes L2-normalized or computes dot product).
 */
float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    double dot = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        double va = static_cast<double>(a[i]);
        double vb = static_cast<double>(b[i]);
        dot += va * vb;
        norm_a += va * va;
        norm_b += vb * vb;
    }
    double denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    if (denom < 1e-12) return 0.0f;
    return static_cast<float>(dot / denom);
}

/**
 * @brief Maps raw negative SQLite FTS5 BM25 score (e.g. -2.5 is better than -0.5)
 * into normalized range [0.0, 1.0], where higher values mean better text relevance.
 */
float sigmoid_normalize_bm25(float raw_bm25) {
    // In SQLite FTS5, bm25() returns negative values (e.g. -3.0 = high relevance, 0.0 = low relevance).
    // Invert sign so higher relevance is positive, then apply logistic sigmoid.
    double x = -static_cast<double>(raw_bm25);
    return static_cast<float>(1.0 / (1.0 + std::exp(-x)));
}

/**
 * @brief Maps cosine similarity from [-1.0, 1.0] into [0.0, 1.0].
 */
float normalize_cosine_similarity(float sim) {
    float clamped = std::max(-1.0f, std::min(1.0f, sim));
    return (clamped + 1.0f) * 0.5f;
}

}  // namespace vinox::storage
