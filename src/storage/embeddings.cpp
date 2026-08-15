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

}  // namespace vinox::storage
