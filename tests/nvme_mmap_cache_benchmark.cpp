#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <windows.h>
#include <psapi.h>

#include "vinox/openvino.h"
#include "vinox/vinox.h"

namespace fs = std::filesystem;

struct BenchRunMetrics {
    double load_ms{0.0};
    double first_token_ttft_ms{0.0};
    double gen_ms{0.0};
    int tokens{0};
    double tok_per_sec{0.0};
    uint64_t peak_working_set_bytes{0};
    uint64_t private_bytes{0};
    uint64_t io_read_bytes{0};
    uint64_t io_read_ops{0};
    double cpu_time_ms{0.0};
};

struct ConfigBenchmarkResult {
    std::string name;
    BenchRunMetrics run1_cold;
    BenchRunMetrics run2_warm;
};

struct StreamCtx {
    int tokens{0};
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point first_token_time;
    bool has_first_token{false};
};

static int VINOX_CALL bench_stream_cb(vinox_stream_channel channel, const char* text, size_t size, void* user_data) {
    (void)channel; (void)text;
    if (size == 0) return 0;
    auto* ctx = static_cast<StreamCtx*>(user_data);
    if (!ctx->has_first_token) {
        ctx->first_token_time = std::chrono::high_resolution_clock::now();
        ctx->has_first_token = true;
    }
    ctx->tokens++;
    return 0;
}

static BenchRunMetrics run_single_eval(
    const std::string& model_path,
    const std::string& device,
    int enable_mmap,
    int enable_cache,
    const std::string& cache_dir,
    int max_tokens = 32
) {
    BenchRunMetrics m{};

    // Initial Process Baseline
    IO_COUNTERS io_before{};
    GetProcessIoCounters(GetCurrentProcess(), &io_before);

    FILETIME ft_create, ft_exit, ft_kernel_start, ft_user_start;
    GetProcessTimes(GetCurrentProcess(), &ft_create, &ft_exit, &ft_kernel_start, &ft_user_start);

    // 1. Model Load
    auto t0 = std::chrono::high_resolution_clock::now();

    vinox_model_options m_opts{};
    m_opts.struct_size = sizeof(m_opts);
    m_opts.model_path = model_path.c_str();
    m_opts.device = device.c_str();
    m_opts.enable_mmap = enable_mmap;
    m_opts.enable_cache = enable_cache;
    m_opts.cache_dir = enable_cache ? cache_dir.c_str() : nullptr;

    vinox_model* model = nullptr;
    vinox_status st = vinox_model_load(&m_opts, &model);
    auto t1 = std::chrono::high_resolution_clock::now();
    m.load_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (st != VINOX_STATUS_OK || !model) {
        std::cerr << "Load failed: " << (vinox_openvino_last_error() ? vinox_openvino_last_error() : "unknown") << std::endl;
        return m;
    }

    // 2. Generation & TTFT
    vinox_generation_options gen_opts{};
    gen_opts.struct_size = sizeof(gen_opts);
    gen_opts.prompt = "User: Write a short C++ function that calculates the Fibonacci sequence.\nAssistant:";
    gen_opts.max_new_tokens = max_tokens;
    gen_opts.temperature = 0.1f;
    gen_opts.reasoning_mode = VINOX_REASONING_NONE;
    gen_opts.reasoning_can_disable = 1;

    StreamCtx sctx{};
    sctx.start_time = std::chrono::high_resolution_clock::now();

    vinox_status gen_st = vinox_model_generate_stream(model, &gen_opts, bench_stream_cb, &sctx);
    auto t2 = std::chrono::high_resolution_clock::now();

    if (gen_st == VINOX_STATUS_OK) {
        m.tokens = sctx.tokens;
        if (sctx.has_first_token) {
            m.first_token_ttft_ms = std::chrono::duration<double, std::milli>(sctx.first_token_time - sctx.start_time).count();
        }
        m.gen_ms = std::chrono::duration<double, std::milli>(t2 - sctx.start_time).count();
        if (m.gen_ms > 0) {
            m.tok_per_sec = (m.tokens * 1000.0) / m.gen_ms;
        }
    } else {
        std::cerr << "Gen failed: " << (vinox_openvino_last_error() ? vinox_openvino_last_error() : "unknown") << std::endl;
    }

    // Memory usage
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        m.peak_working_set_bytes = pmc.PeakWorkingSetSize;
        m.private_bytes = pmc.PrivateUsage;
    }

    // I/O counters delta
    IO_COUNTERS io_after{};
    GetProcessIoCounters(GetCurrentProcess(), &io_after);
    m.io_read_bytes = (io_after.ReadTransferCount >= io_before.ReadTransferCount) ? (io_after.ReadTransferCount - io_before.ReadTransferCount) : 0;
    m.io_read_ops = (io_after.ReadOperationCount >= io_before.ReadOperationCount) ? (io_after.ReadOperationCount - io_before.ReadOperationCount) : 0;

    // CPU time delta
    FILETIME ft_kernel_end, ft_user_end;
    GetProcessTimes(GetCurrentProcess(), &ft_create, &ft_exit, &ft_kernel_end, &ft_user_end);
    ULARGE_INTEGER k_start{.LowPart = ft_kernel_start.dwLowDateTime, .HighPart = ft_kernel_start.dwHighDateTime};
    ULARGE_INTEGER k_end{.LowPart = ft_kernel_end.dwLowDateTime, .HighPart = ft_kernel_end.dwHighDateTime};
    ULARGE_INTEGER u_start{.LowPart = ft_user_start.dwLowDateTime, .HighPart = ft_user_start.dwHighDateTime};
    ULARGE_INTEGER u_end{.LowPart = ft_user_end.dwLowDateTime, .HighPart = ft_user_end.dwHighDateTime};
    uint64_t total_cpu_100ns = (k_end.QuadPart - k_start.QuadPart) + (u_end.QuadPart - u_start.QuadPart);
    m.cpu_time_ms = static_cast<double>(total_cpu_100ns) / 10000.0;

    vinox_model_destroy(model);
    return m;
}

static void print_metrics(const std::string& label, const BenchRunMetrics& m) {
    std::cout << std::left << std::setw(18) << label
              << " | Load: " << std::right << std::setw(7) << std::fixed << std::setprecision(1) << m.load_ms << " ms"
              << " | TTFT: " << std::setw(6) << std::fixed << std::setprecision(1) << m.first_token_ttft_ms << " ms"
              << " | Speed: " << std::setw(5) << std::fixed << std::setprecision(1) << m.tok_per_sec << " t/s"
              << " | PeakRAM: " << std::setw(6) << (m.peak_working_set_bytes / (1024 * 1024)) << " MB"
              << " | DiskRead: " << std::setw(6) << (m.io_read_bytes / (1024 * 1024)) << " MB"
              << " | CPU: " << std::setw(6) << std::fixed << std::setprecision(0) << m.cpu_time_ms << " ms"
              << "\n";
}

int main(int argc, char* argv[]) {
    std::cout << "=========================================================================================\n";
    std::cout << "  VINOX NVMe Optimization Differential Benchmark (mmap OFF vs mmap ON vs Blob Cache)     \n";
    std::cout << "=========================================================================================\n";

    std::string model_path = "C:\\ai\\models\\OpenVINO\\Qwen2.5-Coder-0.5B-fp16-test-ov";
    std::string device = "NPU";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--device" && i + 1 < argc) {
            device = argv[++i];
        } else if (arg == "CPU" || arg == "NPU" || arg == "GPU") {
            device = arg;
        } else if (arg.find("\\") != std::string::npos || arg.find("/") != std::string::npos) {
            model_path = arg;
        }
    }

    if (!fs::exists(model_path)) {
        std::cerr << "Model path does not exist: " << model_path << "\n";
        return 1;
    }

    // Hardware Detection
    vinox_storage_info hw{};
    hw.struct_size = sizeof(hw);
    vinox_storage_detect(model_path.c_str(), &hw);
    std::cout << "Target Hardware: " << hw.device_name << " (" << hw.bus_type_name
              << ", NVMe=" << (hw.is_nvme ? "TRUE" : "FALSE")
              << ", Total=" << (hw.total_bytes / (1024 * 1024 * 1024)) << " GB"
              << ", Free=" << (hw.free_bytes / (1024 * 1024 * 1024)) << " GB)\n";
    std::cout << "Target Device:   " << device << "\n";
    std::cout << "Target Model:    " << model_path << "\n\n";

    std::string cache_base = "C:\\ai\\openvino\\cache\\differential_bench_" + device;
    std::error_code ec;
    fs::create_directories(cache_base, ec);

    // CONFIG 1: mmap OFF / cache OFF
    std::cout << ">>> [CONFIG 1] MMAP: OFF | BLOB CACHE: OFF (" << device << ")\n";
    std::cout << "    (Weights copied through OS buffer caches, Full graph recompilation every load)\n";
    BenchRunMetrics c1_run1 = run_single_eval(model_path, device, 0, 0, "");
    print_metrics("  1st Run (Cold)", c1_run1);
    BenchRunMetrics c1_run2 = run_single_eval(model_path, device, 0, 0, "");
    print_metrics("  2nd Run (Warm)", c1_run2);
    std::cout << "\n";

    // CONFIG 2: mmap ON / cache OFF
    std::cout << ">>> [CONFIG 2] MMAP: ON  | BLOB CACHE: OFF (" << device << ")\n";
    std::cout << "    (Zero-copy memory mapped weights directly from NVMe, Full graph recompilation)\n";
    BenchRunMetrics c2_run1 = run_single_eval(model_path, device, 1, 0, "");
    print_metrics("  1st Run (Cold)", c2_run1);
    BenchRunMetrics c2_run2 = run_single_eval(model_path, device, 1, 0, "");
    print_metrics("  2nd Run (Warm)", c2_run2);
    std::cout << "\n";

    // CONFIG 3: mmap ON / cache ON
    std::cout << ">>> [CONFIG 3] MMAP: ON  | BLOB CACHE: ON (" << device << ")\n";
    std::cout << "    (Zero-copy memory mapped weights + Precompiled device graph blob on NVMe)\n";
    std::string c3_cache = cache_base + "\\c3";
    fs::remove_all(c3_cache, ec);
    fs::create_directories(c3_cache, ec);

    BenchRunMetrics c3_run1 = run_single_eval(model_path, device, 1, 1, c3_cache);
    print_metrics("  1st Run (Compile)", c3_run1);
    BenchRunMetrics c3_run2 = run_single_eval(model_path, device, 1, 1, c3_cache);
    print_metrics("  2nd Run (Cached)", c3_run2);
    std::cout << "\n";

    // SUMMARY COMPARISON TABLE
    std::cout << "=========================================================================================\n";
    std::cout << "  DIFFERENTIAL IMPACT ANALYSIS SUMMARY TABLE\n";
    std::cout << "=========================================================================================\n";
    std::cout << std::left
              << std::setw(28) << "Configuration"
              << std::setw(14) << "Cold Load"
              << std::setw(14) << "Warm Load"
              << std::setw(14) << "Cold TTFT"
              << std::setw(14) << "Warm TTFT"
              << std::setw(12) << "Peak RAM"
              << "\n";
    std::cout << "-----------------------------------------------------------------------------------------\n";

    auto print_row = [](const std::string& name, const BenchRunMetrics& r1, const BenchRunMetrics& r2) {
        std::cout << std::left << std::setw(28) << name
                  << std::right
                  << std::setw(10) << std::fixed << std::setprecision(1) << r1.load_ms << " ms  "
                  << std::setw(10) << std::fixed << std::setprecision(1) << r2.load_ms << " ms  "
                  << std::setw(10) << std::fixed << std::setprecision(1) << r1.first_token_ttft_ms << " ms  "
                  << std::setw(10) << std::fixed << std::setprecision(1) << r2.first_token_ttft_ms << " ms  "
                  << std::setw(8) << (r2.peak_working_set_bytes / (1024 * 1024)) << " MB"
                  << "\n";
    };

    print_row("mmap OFF / cache OFF", c1_run1, c1_run2);
    print_row("mmap ON  / cache OFF", c2_run1, c2_run2);
    print_row("mmap ON  / cache ON", c3_run1, c3_run2);
    std::cout << "=========================================================================================\n";

    double speedup_warm_load = (c1_run2.load_ms > 0 && c3_run2.load_ms > 0) ? (c1_run2.load_ms / c3_run2.load_ms) : 1.0;
    std::cout << "  Key Takeaway: Blob Cache reduces Warm Model Load Time by "
              << std::fixed << std::setprecision(1) << speedup_warm_load << "x!\n";
    std::cout << "=========================================================================================\n";

    return 0;
}
