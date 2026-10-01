#include <iostream>
#include <string>
#include <cstring>
#include <cassert>
#include <filesystem>

#include "vinox/openvino.h"
#include "vinox/vinox.h"

struct StreamCollector {
    std::string text;
    int deltas{0};
};

static int VINOX_CALL test_stream_cb(vinox_stream_channel channel, const char* text, size_t size, void* user_data) {
    (void)channel;
    auto* sc = static_cast<StreamCollector*>(user_data);
    if (text && size > 0) {
        sc->text.append(text, size);
        sc->deltas++;
    }
    return 0;
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "  VINOX NVMe Detection, mmap & Blob Cache Smoke Test                           \n";
    std::cout << "================================================================================\n";

    // TEST 1: Storage Hardware Detection
    {
        std::cout << "[TEST 01] Storage Hardware Detection (IOCTL_STORAGE_QUERY_PROPERTY) ... ";
        vinox_storage_info info{};
        info.struct_size = sizeof(info);
        vinox_status st = vinox_storage_detect("C:\\ai\\models\\OpenVINO", &info);
        assert(st == VINOX_STATUS_OK);
        assert(info.total_bytes > 0);
        std::cout << "[ PASS ]\n";
        std::cout << "          - Bus Type:     " << info.bus_type_name << "\n";
        std::cout << "          - Device Name:  " << (info.device_name[0] ? info.device_name : "Generic") << "\n";
        std::cout << "          - Is NVMe:      " << (info.is_nvme ? "true" : "false") << "\n";
        std::cout << "          - Is SSD:       " << (info.is_ssd ? "true" : "false") << "\n";
        std::cout << "          - Capacity:     " << (info.total_bytes / (1024 * 1024 * 1024)) << " GB\n";
        std::cout << "          - Free:         " << (info.free_bytes / (1024 * 1024 * 1024)) << " GB\n";
    }

    // TEST 2: Mock Model with NVMe Options (Contract & ABI Verification)
    {
        std::cout << "[TEST 02] Mock Model with NVMe & Cache Options ... ";
        vinox_model_options opts{};
        opts.struct_size = sizeof(opts);
        opts.model_path = "mock";
        opts.device = "CPU";
        opts.enable_mmap = 1;
        opts.enable_cache = 1;
        opts.cache_dir = "C:\\ai\\openvino\\cache\\smoke_blobs";

        vinox_model* model = nullptr;
        vinox_status st = vinox_model_load(&opts, &model);
        assert(st == VINOX_STATUS_OK);
        assert(model != nullptr);

        vinox_model_destroy(model);
        std::cout << "[ PASS ]\n";
    }

    // TEST 3: Live Model with NVMe mmap & Blob Cache Generation
    {
        std::cout << "[TEST 03] Live Model In-Memory Mapping (mmap) & Blob Cache Generation ... ";
        const char* model_dir = "C:\\ai\\models\\OpenVINO\\Qwen2.5-Coder-0.5B-fp16-test-ov";
        if (std::filesystem::exists(model_dir)) {
            std::string cache_path = "C:\\ai\\openvino\\cache\\smoke_blobs";
            std::filesystem::create_directories(cache_path);

            vinox_model_options opts{};
            opts.struct_size = sizeof(opts);
            opts.model_path = model_dir;
            opts.device = "CPU";
            opts.enable_mmap = 1;
            opts.enable_cache = 1;
            opts.cache_dir = cache_path.c_str();

            vinox_model* model = nullptr;
            vinox_status load_st = vinox_model_load(&opts, &model);
            assert(load_st == VINOX_STATUS_OK);
            assert(model != nullptr);

            vinox_generation_options gen_opts{};
            std::memset(&gen_opts, 0, sizeof(gen_opts));
            gen_opts.struct_size = sizeof(gen_opts);
            gen_opts.prompt = "User: Hello\nAssistant:";
            gen_opts.max_new_tokens = 4;
            gen_opts.temperature = 0.1f;
            gen_opts.reasoning_mode = VINOX_REASONING_NONE;
            gen_opts.reasoning_can_disable = 1;

            StreamCollector sc;
            vinox_status gen_st = vinox_model_generate_stream(model, &gen_opts, test_stream_cb, &sc);
            if (gen_st != VINOX_STATUS_OK) {
                std::cerr << "Generation failed with code " << static_cast<int>(gen_st) << ": " << (vinox_openvino_last_error() ? vinox_openvino_last_error() : "unknown") << std::endl;
            }
            assert(gen_st == VINOX_STATUS_OK);
            assert(sc.deltas > 0);

            vinox_model_destroy(model);

            // Verify cache files were generated
            bool found_blob = false;
            for (const auto& entry : std::filesystem::directory_iterator(cache_path)) {
                if (entry.path().extension() == ".blob") {
                    found_blob = true;
                    break;
                }
            }
            assert(found_blob);
            std::cout << "[ PASS ] (Generated " << sc.deltas << " tokens, blob cached)\n";
        } else {
            std::cout << "[ SKIP ] (model fixture not present)\n";
        }
    }

    std::cout << "================================================================================\n";
    std::cout << "  ALL NVMe, MMAP & BLOB CACHE TESTS PASSED! 🟢⚡                                \n";
    std::cout << "================================================================================\n";
    return 0;
}
