#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "vinox/openvino.h"
#include "vinox/vinox.h"

namespace {

struct Arguments {
    std::string model_path;
    std::string prompt;
    std::string device = "CPU";
    std::uint64_t max_new_tokens = 32;
    float temperature = 0.0f;
    float top_p = 1.0f;
};

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  vinox-cli\n"
        << "  vinox-cli --model <path> --prompt <text> "
           "[--device CPU] [--max-new-tokens 32] [--temperature 0.7] [--top-p 0.9]\n";
}

bool parse_unsigned(std::string_view text, std::uint64_t& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_float(std::string_view text, float& value) {
    try {
        size_t pos = 0;
        value = std::stof(std::string(text), &pos);
        return pos == text.size();
    } catch (...) {
        return false;
    }
}

bool parse_arguments(int argc, char* argv[], Arguments& arguments) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help") {
            print_usage();
            return false;
        }
        if (index + 1 >= argc) {
            std::cerr << "Missing value for " << argument << '\n';
            return false;
        }

        const std::string value = argv[++index];
        if (argument == "--model") {
            arguments.model_path = value;
        } else if (argument == "--prompt") {
            arguments.prompt = value;
        } else if (argument == "--device") {
            arguments.device = value;
        } else if (argument == "--max-new-tokens") {
            if (!parse_unsigned(value, arguments.max_new_tokens)) {
                std::cerr << "Invalid token count: " << value << '\n';
                return false;
            }
        } else if (argument == "--temperature") {
            if (!parse_float(value, arguments.temperature)) {
                std::cerr << "Invalid temperature: " << value << '\n';
                return false;
            }
        } else if (argument == "--top-p") {
            if (!parse_float(value, arguments.top_p)) {
                std::cerr << "Invalid top-p: " << value << '\n';
                return false;
            }
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return false;
        }
    }
    return true;
}

int write_text(const char* text, size_t text_size, void*) {
    std::cout.write(text, static_cast<std::streamsize>(text_size));
    std::cout.flush();
    return 0;
}

int print_version() {
    vinox_version_info version{};
    version.struct_size = sizeof(version);

    if (vinox_get_version(&version) != VINOX_STATUS_OK) {
        std::cerr << "Failed to query vinox version\n";
        return 1;
    }

    std::cout << "vinox " << version.version_string
              << " (ABI " << version.abi_version << ")\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 1) {
        return print_version();
    }

    Arguments arguments;
    if (!parse_arguments(argc, argv, arguments)) {
        return 2;
    }
    if (arguments.model_path.empty() || arguments.prompt.empty()) {
        print_usage();
        return 2;
    }

    vinox_model_options model_options{};
    model_options.struct_size = sizeof(model_options);
    model_options.model_path = arguments.model_path.c_str();
    model_options.device = arguments.device.c_str();

    vinox_model* model = nullptr;
    const vinox_status load_status = vinox_model_load(&model_options, &model);
    if (load_status != VINOX_STATUS_OK) {
        std::cerr << "Model load failed: " << vinox_openvino_last_error() << '\n';
        return 1;
    }

    vinox_generation_options generation_options{};
    generation_options.struct_size = sizeof(generation_options);
    generation_options.prompt = arguments.prompt.c_str();
    generation_options.max_new_tokens = arguments.max_new_tokens;
    generation_options.temperature = arguments.temperature;
    generation_options.top_p = arguments.top_p;

    const vinox_status generation_status = vinox_model_generate(
        model,
        &generation_options,
        write_text,
        nullptr
    );
    vinox_model_destroy(model);

    if (generation_status != VINOX_STATUS_OK) {
        std::cerr << "\nGeneration failed: " << vinox_openvino_last_error() << '\n';
        return 1;
    }
    std::cout << '\n';
    return 0;
}
