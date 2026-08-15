#include "vinox/serving.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ModelEntry {
    std::string model_id;
    std::string display_name;
    std::string local_path;
    std::string default_device{"CPU"};
    uint64_t context_length{4096};
    vinox_model_state state{VINOX_MODEL_STATE_UNLOADED};
};

struct vinox_model_registry {
    std::mutex mutex;
    std::vector<ModelEntry> models;
};

namespace {

thread_local std::string last_error;

vinox_status fail_arg(const char* message) {
    last_error = message;
    return VINOX_STATUS_INVALID_ARGUMENT;
}

vinox_status fail_abi(const char* message) {
    last_error = message;
    return VINOX_STATUS_INCOMPATIBLE_ABI;
}

vinox_status fail_runtime(const char* message) {
    last_error = message;
    return VINOX_STATUS_RUNTIME_ERROR;
}

std::string extract_json_string(const std::string& json, const std::string& key) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    if (std::regex_search(json, match, pattern) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

uint64_t extract_json_uint64(const std::string& json, const std::string& key, uint64_t default_val) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*(\\d+)");
    std::smatch match;
    if (std::regex_search(json, match, pattern) && match.size() > 1) {
        try {
            return std::stoull(match[1].str());
        } catch (...) {}
    }
    return default_val;
}

bool parse_manifest_file(const fs::path& path, ModelEntry& entry) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    entry.model_id = extract_json_string(content, "model_id");
    entry.display_name = extract_json_string(content, "display_name");
    entry.local_path = extract_json_string(content, "local_path");
    
    std::string device = extract_json_string(content, "default_device");
    entry.default_device = device.empty() ? "CPU" : device;

    entry.context_length = extract_json_uint64(content, "context_length", 4096);
    entry.state = VINOX_MODEL_STATE_UNLOADED;

    if (entry.model_id.empty()) {
        entry.model_id = path.stem().string();
    }
    if (entry.display_name.empty()) {
        entry.display_name = entry.model_id;
    }
    if (entry.local_path.empty()) {
        entry.local_path = path.parent_path().string();
    }

    return !entry.model_id.empty();
}

}  // namespace

vinox_status vinox_model_registry_create(vinox_model_registry** registry) {
    if (registry == nullptr) {
        return fail_arg("registry output pointer cannot be null");
    }
    try {
        *registry = new vinox_model_registry();
        last_error.clear();
        return VINOX_STATUS_OK;
    } catch (...) {
        return fail_runtime("Failed to allocate vinox_model_registry");
    }
}

vinox_status vinox_model_registry_scan(
    vinox_model_registry* registry,
    const char* directory_path,
    size_t* count_out
) {
    if (registry == nullptr) {
        return fail_arg("registry handle cannot be null");
    }
    if (directory_path == nullptr || directory_path[0] == '\0') {
        return fail_arg("directory_path cannot be null or empty");
    }

    std::error_code ec;
    fs::path dir(directory_path);
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return fail_arg("directory_path does not exist or is not a directory");
    }

    size_t added_count = 0;
    std::lock_guard<std::mutex> lock(registry->mutex);

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.is_regular_file() && entry.path().filename().string() == "model-manifest.json") {
            ModelEntry model_info;
            if (parse_manifest_file(entry.path(), model_info)) {
                // Check if already registered by model_id
                bool exists = false;
                for (const auto& m : registry->models) {
                    if (m.model_id == model_info.model_id) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    registry->models.push_back(model_info);
                    added_count++;
                }
            }
        }
    }

    if (count_out) {
        *count_out = registry->models.size();
    }

    last_error.clear();
    return VINOX_STATUS_OK;
}

vinox_status vinox_model_registry_register_manifest(
    vinox_model_registry* registry,
    const char* manifest_json_path
) {
    if (registry == nullptr) {
        return fail_arg("registry handle cannot be null");
    }
    if (manifest_json_path == nullptr || manifest_json_path[0] == '\0') {
        return fail_arg("manifest_json_path cannot be null or empty");
    }

    fs::path path(manifest_json_path);
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return fail_arg("manifest file does not exist");
    }

    ModelEntry entry;
    if (!parse_manifest_file(path, entry)) {
        return fail_arg("failed to parse manifest file");
    }

    std::lock_guard<std::mutex> lock(registry->mutex);
    for (auto& m : registry->models) {
        if (m.model_id == entry.model_id) {
            m = entry;
            last_error.clear();
            return VINOX_STATUS_OK;
        }
    }

    registry->models.push_back(entry);
    last_error.clear();
    return VINOX_STATUS_OK;
}

vinox_status vinox_model_registry_get_count(
    const vinox_model_registry* registry,
    size_t* count_out
) {
    if (registry == nullptr) {
        return fail_arg("registry handle cannot be null");
    }
    if (count_out == nullptr) {
        return fail_arg("count_out pointer cannot be null");
    }

    std::lock_guard<std::mutex> lock(const_cast<vinox_model_registry*>(registry)->mutex);
    *count_out = registry->models.size();
    last_error.clear();
    return VINOX_STATUS_OK;
}

vinox_status vinox_model_registry_get_info(
    const vinox_model_registry* registry,
    size_t index,
    vinox_model_info* info_out
) {
    if (registry == nullptr) {
        return fail_arg("registry handle cannot be null");
    }
    if (info_out == nullptr) {
        return fail_arg("info_out pointer cannot be null");
    }
    if (info_out->struct_size < VINOX_MODEL_INFO_MIN_SIZE) {
        return fail_abi("info_out->struct_size is smaller than VINOX_MODEL_INFO_MIN_SIZE");
    }

    std::lock_guard<std::mutex> lock(const_cast<vinox_model_registry*>(registry)->mutex);
    if (index >= registry->models.size()) {
        return fail_arg("model index out of range");
    }

    const auto& entry = registry->models[index];
    info_out->model_id = entry.model_id.c_str();
    info_out->display_name = entry.display_name.c_str();
    info_out->local_path = entry.local_path.c_str();
    info_out->default_device = entry.default_device.c_str();
    info_out->context_length = entry.context_length;
    info_out->state = static_cast<uint32_t>(entry.state);

    last_error.clear();
    return VINOX_STATUS_OK;
}

void vinox_model_registry_destroy(vinox_model_registry* registry) {
    delete registry;
}

const char* vinox_serving_last_error(void) {
    return last_error.c_str();
}
