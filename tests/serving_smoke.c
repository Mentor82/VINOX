#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vinox/serving.h"

int main(void) {
    vinox_model_registry* registry = NULL;
    vinox_model_info info = {0};
    size_t count = 0;

    // 1. Create registry
    if (vinox_model_registry_create(&registry) != VINOX_STATUS_OK || registry == NULL) {
        return 1;
    }

    // 2. Count on empty registry
    if (vinox_model_registry_get_count(registry, &count) != VINOX_STATUS_OK || count != 0) {
        vinox_model_registry_destroy(registry);
        return 2;
    }

    // 3. Register single manifest file (using schemas/model-manifest.schema.json or dummy manifest)
    // Registering non-existent file returns INVALID_ARGUMENT + last_error
    if (vinox_model_registry_register_manifest(registry, "non_existent.json") != VINOX_STATUS_INVALID_ARGUMENT) {
        vinox_model_registry_destroy(registry);
        return 3;
    }
    if (vinox_serving_last_error() == NULL || strlen(vinox_serving_last_error()) == 0) {
        vinox_model_registry_destroy(registry);
        return 4;
    }

    // 4. Out of range info lookup
    info.struct_size = sizeof(info);
    if (vinox_model_registry_get_info(registry, 0, &info) != VINOX_STATUS_INVALID_ARGUMENT) {
        vinox_model_registry_destroy(registry);
        return 5;
    }

    // 5. Incompatible struct size for info
    info.struct_size = 0;
    if (vinox_model_registry_get_info(registry, 0, &info) != VINOX_STATUS_INCOMPATIBLE_ABI) {
        vinox_model_registry_destroy(registry);
        return 6;
    }

    vinox_model_registry_destroy(registry);
    return 0;
}
