#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <algorithm>

#include "vinox/embedding.h"
#include "vinox/embedding.hpp"
#include "vinox/vinox.h"

namespace {

float cosine_sim(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    double dot = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
    }
    return static_cast<float>(dot);
}

} // namespace

int main() {
    std::cout << "================================================================================\n";
    std::cout << "             VINOX Decoupled Embedding Engine Smoke Test                       \n";
    std::cout << "================================================================================\n";

    // 1. Resolve model path with portability fallback
    const char* env_path = std::getenv("VINOX_EMBEDDING_MODEL");
    std::string model_path = env_path ? env_path : "C:\\ai\\models\\OpenVINO\\Qwen3-Embedding-0.6B-fp16-ov";

    std::filesystem::path xml_path = std::filesystem::path(model_path) / "openvino_model.xml";
    if (!std::filesystem::exists(xml_path)) {
        std::cout << "[SKIP] Embedding model not found at: " << model_path << "\n";
        std::cout << "       Set VINOX_EMBEDDING_MODEL environment variable to run this test.\n";
        return 0;
    }

    std::cout << "Target Model: " << model_path << "\n";

    // 2. Initialize Engine via C++ RAII Wrapper
    vinox::embedding::EmbeddingEngine engine;
    vinox_status st = engine.load(model_path, "CPU", VINOX_EMBEDDING_POOLING_AUTO, VINOX_EMBEDDING_NORM_AUTO, true, true);
    if (st != VINOX_STATUS_OK) {
        std::cerr << "FAILED: engine.load returned " << st << ": " << vinox_embedding_last_error() << "\n";
        return 1;
    }
    std::cout << "[PASS 01] Decoupled Embedding Engine initialized successfully.\n";

    // 3. Dynamic Dimension & Provenance Info Check
    size_t dim = engine.dimension();
    std::cout << "Detected Dimension: " << dim << "\n";
    if (dim == 0) {
        std::cerr << "FAILED: Detected dimension is 0!\n";
        return 1;
    }

    vinox_embedding_info info{};
    info.struct_size = sizeof(info);
    char pool_buf[512] = {0};
    if (vinox_embedding_get_info(engine.get(), &info, pool_buf, sizeof(pool_buf)) != VINOX_STATUS_OK) {
        std::cerr << "FAILED: vinox_embedding_get_info returned error\n";
        return 1;
    }

    std::cout << "  - Model ID:       " << (info.model_id ? info.model_id : "") << "\n";
    std::cout << "  - Device:         " << (info.device ? info.device : "") << "\n";
    std::cout << "  - Pooling Mode:   " << info.pooling_mode << "\n";
    std::cout << "  - Normalization:  " << info.normalization << "\n";
    std::cout << "  - Space ID:       " << (info.space_id ? info.space_id : "") << "\n";

    if (!info.space_id || strlen(info.space_id) != 64) {
        std::cerr << "FAILED: Space ID must be a 64-character SHA-256 hex string!\n";
        return 1;
    }
    std::cout << "[PASS 02] Dynamic dimension & SHA-256 provenance verified.\n";

    // 4. Deterministic Single Generation & Finite/Norm Checks
    std::string text_a = "OpenVINO high-performance neural network inference engine";
    std::vector<float> vec_a;
    st = engine.generate(text_a, vec_a);
    if (st != VINOX_STATUS_OK || vec_a.size() != dim) {
        std::cerr << "FAILED: engine.generate returned " << st << "\n";
        return 1;
    }

    // Check finite
    for (size_t i = 0; i < vec_a.size(); ++i) {
        if (std::isnan(vec_a[i]) || std::isinf(vec_a[i])) {
            std::cerr << "FAILED: Non-finite value in vector at index " << i << "\n";
            return 1;
        }
    }

    // Check L2 norm ≈ 1.0
    double norm_a = 0.0;
    for (float v : vec_a) norm_a += static_cast<double>(v) * static_cast<double>(v);
    norm_a = std::sqrt(norm_a);
    if (std::abs(norm_a - 1.0) > 1e-4) {
        std::cerr << "FAILED: Vector L2 norm is not 1.0! Actual norm: " << norm_a << "\n";
        return 1;
    }
    std::cout << "[PASS 03] Finite values and unit L2-norm verified (|norm - 1.0| < 1e-4).\n";

    // 5. Batch == Single Consistency Check
    std::string text_b = "Optimized deep learning runtime and model execution";
    std::vector<float> vec_b;
    st = engine.generate(text_b, vec_b);
    if (st != VINOX_STATUS_OK) return 1;

    std::vector<std::vector<float>> batch_vecs;
    st = engine.generate_batch({text_a, text_b}, batch_vecs);
    if (st != VINOX_STATUS_OK || batch_vecs.size() != 2) {
        std::cerr << "FAILED: generate_batch returned " << st << "\n";
        return 1;
    }

    float max_diff_a = 0.0f;
    for (size_t i = 0; i < dim; ++i) {
        max_diff_a = std::max(max_diff_a, std::abs(vec_a[i] - batch_vecs[0][i]));
    }
    float max_diff_b = 0.0f;
    for (size_t i = 0; i < dim; ++i) {
        max_diff_b = std::max(max_diff_b, std::abs(vec_b[i] - batch_vecs[1][i]));
    }

    std::cout << "Batch vs Single Max Diff A: " << max_diff_a << ", Max Diff B: " << max_diff_b << "\n";
    if (max_diff_a > 1e-5f || max_diff_b > 1e-5f) {
        std::cerr << "FAILED: Batch vs Single results diverge beyond epsilon 1e-5!\n";
        return 1;
    }
    std::cout << "[PASS 04] Batch == Single execution consistency verified.\n";

    // 6. Model Acceptance & Semantic Evidence Test
    std::string text_c = "Recipe for baking fresh blueberry cheesecake in an oven";
    std::vector<float> vec_c;
    st = engine.generate(text_c, vec_c);
    if (st != VINOX_STATUS_OK) return 1;

    float sim_ab = cosine_sim(vec_a, vec_b);
    float sim_ac = cosine_sim(vec_a, vec_c);

    std::cout << "Semantic Similar Similarity (AI Runtime vs Deep Learning):   " << sim_ab << "\n";
    std::cout << "Semantic Unrelated Similarity (AI Runtime vs Cheesecake):      " << sim_ac << "\n";

    if (sim_ab <= sim_ac) {
        std::cerr << "FAILED: Semantic similarity violated: Sim(A,B) <= Sim(A,C)!\n";
        return 1;
    }
    std::cout << "[PASS 05] Semantic similarity acceptance test passed: Sim(A,B) > Sim(A,C)\n";

    std::cout << "\nALL 5 EMBEDDING SMOKE TESTS PASSED CLEANLY!\n";
    return 0;
}
