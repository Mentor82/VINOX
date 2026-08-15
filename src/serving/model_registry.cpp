#include "vinox/serving.h"

#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

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
    std::deque<ModelEntry> models; // deque guarantees pointer stability on push_back
};

namespace {

#define VINOX_FIELD_PRESENT(ptr, member) \
    ((ptr)->struct_size >= (offsetof(std::remove_pointer_t<decltype(ptr)>, member) + sizeof((ptr)->member)))

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

bool validate_and_parse_manifest(const fs::path& path, ModelEntry& entry, std::string& err_msg) {
    std::ifstream file(path);
    if (!file.is_open()) {
        err_msg = "Failed to open manifest file: " + path.string();
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& ex) {
        err_msg = std::string("Manifest JSON parse error in ") + path.filename().string() + ": " + ex.what();
        return false;
    }

    if (!root.is_object()) {
        err_msg = "Manifest root must be a JSON object in " + path.filename().string();
        return false;
    }

    // 1. Required model_id
    if (!root.contains("model_id") || !root["model_id"].is_string() || root["model_id"].get<std::string>().empty()) {
        err_msg = "Manifest missing required string property 'model_id'";
        return false;
    }
    entry.model_id = root["model_id"].get<std::string>();

    // Pattern check: vendor/name
    static const std::regex id_pattern("^[a-zA-Z0-9_-]+/[a-zA-Z0-9_.-]+$");
    if (!std::regex_match(entry.model_id, id_pattern)) {
        err_msg = "Invalid model_id format: '" + entry.model_id + "' must match pattern vendor/name";
        return false;
    }

    // 2. Required display_name
    if (!root.contains("display_name") || !root["display_name"].is_string() || root["display_name"].get<std::string>().empty()) {
        err_msg = "Manifest missing required string property 'display_name'";
        return false;
    }
    entry.display_name = root["display_name"].get<std::string>();

    // 3. Required local_path
    if (!root.contains("local_path") || !root["local_path"].is_string() || root["local_path"].get<std::string>().empty()) {
        err_msg = "Manifest missing required string property 'local_path'";
        return false;
    }
    entry.local_path = root["local_path"].get<std::string>();

    // 4. Required context_length (> 0)
    if (!root.contains("context_length") || !root["context_length"].is_number_integer() || root["context_length"].get<int64_t>() <= 0) {
        err_msg = "Manifest missing or invalid integer property 'context_length' (> 0)";
        return false;
    }
    entry.context_length = static_cast<uint64_t>(root["context_length"].get<int64_t>());

    // 5. Required capabilities array
    if (!root.contains("capabilities") || !root["capabilities"].is_array() || root["capabilities"].empty()) {
        err_msg = "Manifest missing required array property 'capabilities' (minItems 1)";
        return false;
    }
    static const std::vector<std::string> valid_caps = {"chat", "structured_output", "tools", "embeddings", "vision"};
    for (const auto& cap_item : root["capabilities"]) {
        if (!cap_item.is_string()) {
            err_msg = "Invalid capability item type in manifest";
            return false;
        }
        std::string cap_str = cap_item.get<std::string>();
        bool valid = false;
        for (const auto& vc : valid_caps) {
            if (cap_str == vc) {
                valid = true;
                break;
            }
        }
        if (!valid) {
            err_msg = "Unknown model capability: '" + cap_str + "'";
            return false;
        }
    }

    // 6. Optional default_device
    if (root.contains("default_device") && root["default_device"].is_string() && !root["default_device"].get<std::string>().empty()) {
        entry.default_device = root["default_device"].get<std::string>();
    } else {
        entry.default_device = "CPU";
    }

    entry.state = VINOX_MODEL_STATE_UNLOADED;
    return true;
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

    std::lock_guard<std::mutex> lock(registry->mutex);

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.is_regular_file() && entry.path().filename().string() == "model-manifest.json") {
            ModelEntry model_info;
            std::string parse_err;
            if (validate_and_parse_manifest(entry.path(), model_info, parse_err)) {
                // Duplicate check by model_id
                bool exists = false;
                for (auto& m : registry->models) {
                    if (m.model_id == model_info.model_id) {
                        m = model_info;
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    registry->models.push_back(model_info);
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
    std::string parse_err;
    if (!validate_and_parse_manifest(path, entry, parse_err)) {
        return fail_arg(parse_err.c_str());
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

    if (VINOX_FIELD_PRESENT(info_out, default_device)) {
        info_out->default_device = entry.default_device.c_str();
    }
    if (VINOX_FIELD_PRESENT(info_out, context_length)) {
        info_out->context_length = entry.context_length;
    }
    if (VINOX_FIELD_PRESENT(info_out, state)) {
        info_out->state = static_cast<uint32_t>(entry.state);
    }

    last_error.clear();
    return VINOX_STATUS_OK;
}

void vinox_model_registry_destroy(vinox_model_registry* registry) {
    delete registry;
}

const char* vinox_serving_last_error(void) {
    return last_error.c_str();
}
