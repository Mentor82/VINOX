#include "vinox/serving.h"

#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <type_traits>
#include <vector>

#include "json.hpp"

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
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    vinox::serving::JsonValue root;
    try {
        root = vinox::serving::JsonParser::parse(content);
    } catch (const std::exception& ex) {
        err_msg = std::string("Manifest JSON parse error in ") + path.filename().string() + ": " + ex.what();
        return false;
    }

    if (root.type != vinox::serving::JsonType::Object) {
        err_msg = "Manifest root must be a JSON object in " + path.filename().string();
        return false;
    }

    const auto& obj = root.object_value;

    // 1. Required model_id
    auto it_id = obj.find("model_id");
    if (it_id == obj.end() || it_id->second.type != vinox::serving::JsonType::String || it_id->second.string_value.empty()) {
        err_msg = "Manifest missing required string property 'model_id'";
        return false;
    }
    entry.model_id = it_id->second.string_value;

    // Pattern check: vendor/name
    static const std::regex id_pattern("^[a-zA-Z0-9_-]+/[a-zA-Z0-9_.-]+$");
    if (!std::regex_match(entry.model_id, id_pattern)) {
        err_msg = "Invalid model_id format: '" + entry.model_id + "' must match pattern vendor/name";
        return false;
    }

    // 2. Required display_name
    auto it_name = obj.find("display_name");
    if (it_name == obj.end() || it_name->second.type != vinox::serving::JsonType::String || it_name->second.string_value.empty()) {
        err_msg = "Manifest missing required string property 'display_name'";
        return false;
    }
    entry.display_name = it_name->second.string_value;

    // 3. Required local_path
    auto it_path = obj.find("local_path");
    if (it_path == obj.end() || it_path->second.type != vinox::serving::JsonType::String || it_path->second.string_value.empty()) {
        err_msg = "Manifest missing required string property 'local_path'";
        return false;
    }
    entry.local_path = it_path->second.string_value;

    // 4. Required context_length (> 0)
    auto it_ctx = obj.find("context_length");
    if (it_ctx == obj.end() || it_ctx->second.type != vinox::serving::JsonType::Number || it_ctx->second.number_value <= 0) {
        err_msg = "Manifest missing or invalid integer property 'context_length' (> 0)";
        return false;
    }
    entry.context_length = static_cast<uint64_t>(it_ctx->second.number_value);

    // 5. Required capabilities array
    auto it_cap = obj.find("capabilities");
    if (it_cap == obj.end() || it_cap->second.type != vinox::serving::JsonType::Array || it_cap->second.array_value.empty()) {
        err_msg = "Manifest missing required array property 'capabilities' (minItems 1)";
        return false;
    }
    static const std::vector<std::string> valid_caps = {"chat", "structured_output", "tools", "embeddings", "vision"};
    for (const auto& cap_item : it_cap->second.array_value) {
        if (cap_item.type != vinox::serving::JsonType::String) {
            err_msg = "Invalid capability item type in manifest";
            return false;
        }
        bool valid = false;
        for (const auto& vc : valid_caps) {
            if (cap_item.string_value == vc) {
                valid = true;
                break;
            }
        }
        if (!valid) {
            err_msg = "Unknown model capability: '" + cap_item.string_value + "'";
            return false;
        }
    }

    // 6. Optional default_device
    auto it_dev = obj.find("default_device");
    if (it_dev != obj.end() && it_dev->second.type == vinox::serving::JsonType::String && !it_dev->second.string_value.empty()) {
        entry.default_device = it_dev->second.string_value;
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
