#ifndef VINOX_SERVING_HPP
#define VINOX_SERVING_HPP

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "vinox/serving.h"

namespace vinox::serving {

struct ModelInfo {
    std::string model_id;
    std::string display_name;
    std::string local_path;
    std::string default_device;
    uint64_t context_length{0};
    vinox_model_state state{VINOX_MODEL_STATE_UNLOADED};
};

class ModelRegistry {
public:
    ModelRegistry() {
        vinox_model_registry* raw = nullptr;
        if (vinox_model_registry_create(&raw) != VINOX_STATUS_OK || !raw) {
            throw std::runtime_error(vinox_serving_last_error());
        }
        registry_.reset(raw);
    }

    size_t scan(const std::string& directory_path) {
        size_t count = 0;
        if (vinox_model_registry_scan(registry_.get(), directory_path.c_str(), &count) != VINOX_STATUS_OK) {
            throw std::runtime_error(vinox_serving_last_error());
        }
        return count;
    }

    void register_manifest(const std::string& manifest_json_path) {
        if (vinox_model_registry_register_manifest(registry_.get(), manifest_json_path.c_str()) != VINOX_STATUS_OK) {
            throw std::runtime_error(vinox_serving_last_error());
        }
    }

    size_t count() const {
        size_t c = 0;
        if (vinox_model_registry_get_count(registry_.get(), &c) != VINOX_STATUS_OK) {
            throw std::runtime_error(vinox_serving_last_error());
        }
        return c;
    }

    ModelInfo get_info(size_t index) const {
        vinox_model_info raw_info{};
        raw_info.struct_size = sizeof(raw_info);
        if (vinox_model_registry_get_info(registry_.get(), index, &raw_info) != VINOX_STATUS_OK) {
            throw std::runtime_error(vinox_serving_last_error());
        }

        ModelInfo info;
        info.model_id = raw_info.model_id ? raw_info.model_id : "";
        info.display_name = raw_info.display_name ? raw_info.display_name : "";
        info.local_path = raw_info.local_path ? raw_info.local_path : "";
        info.default_device = raw_info.default_device ? raw_info.default_device : "";
        info.context_length = raw_info.context_length;
        info.state = static_cast<vinox_model_state>(raw_info.state);
        return info;
    }

    vinox_model_registry* handle() const noexcept {
        return registry_.get();
    }

private:
    struct Deleter {
        void operator()(vinox_model_registry* r) const noexcept {
            vinox_model_registry_destroy(r);
        }
    };
    std::unique_ptr<vinox_model_registry, Deleter> registry_;
};

}  // namespace vinox::serving

#endif
