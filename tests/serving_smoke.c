#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vinox/serving.h"

static void write_test_file(const char* filepath, const char* content) {
    FILE* f = fopen(filepath, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

int main(void) {
    vinox_model_registry* registry = NULL;
    vinox_model_info info = {0};
    size_t count = 0;

    // Create temporary test manifest files
    const char* valid_manifest_1 =
        "{\n"
        "  \"model_id\": \"qwen/qwen2.5-1b-instruct\",\n"
        "  \"display_name\": \"Qwen 2.5 1B Instruct\",\n"
        "  \"local_path\": \"C:\\\\ai\\\\models\\\\Qwen2.5-1B-Instruct\",\n"
        "  \"context_length\": 32768,\n"
        "  \"capabilities\": [\"chat\", \"tools\"]\n"
        "}\n";

    const char* valid_manifest_2 =
        "{\n"
        "  \"model_id\": \"meta/llama-3.2-1b\",\n"
        "  \"display_name\": \"Llama 3.2 1B\",\n"
        "  \"local_path\": \"C:\\\\ai\\\\models\\\\Llama-3.2-1B\",\n"
        "  \"context_length\": 131072,\n"
        "  \"capabilities\": [\"chat\"]\n"
        "}\n";

    const char* invalid_manifest_bad_json = "{ model_id: bad_json }";
    const char* invalid_manifest_missing_id =
        "{\n"
        "  \"display_name\": \"No ID Model\",\n"
        "  \"local_path\": \"C:\\\\ai\\\\models\\\\NoID\",\n"
        "  \"context_length\": 4096,\n"
        "  \"capabilities\": [\"chat\"]\n"
        "}\n";

    write_test_file("test_manifest_valid1.json", valid_manifest_1);
    write_test_file("test_manifest_valid2.json", valid_manifest_2);
    write_test_file("test_manifest_bad_json.json", invalid_manifest_bad_json);
    write_test_file("test_manifest_missing_id.json", invalid_manifest_missing_id);

    // 1. Create registry
    if (vinox_model_registry_create(&registry) != VINOX_STATUS_OK || registry == NULL) {
        return 1;
    }

    // 2. Register valid manifest 1
    if (vinox_model_registry_register_manifest(registry, "test_manifest_valid1.json") != VINOX_STATUS_OK) {
        vinox_model_registry_destroy(registry);
        return 2;
    }
    if (vinox_model_registry_get_count(registry, &count) != VINOX_STATUS_OK || count != 1) {
        vinox_model_registry_destroy(registry);
        return 3;
    }

    // 3. Reject bad JSON
    if (vinox_model_registry_register_manifest(registry, "test_manifest_bad_json.json") != VINOX_STATUS_INVALID_ARGUMENT) {
        vinox_model_registry_destroy(registry);
        return 4;
    }
    if (strstr(vinox_serving_last_error(), "parse error") == NULL) {
        vinox_model_registry_destroy(registry);
        return 5;
    }

    // 4. Reject missing model_id
    if (vinox_model_registry_register_manifest(registry, "test_manifest_missing_id.json") != VINOX_STATUS_INVALID_ARGUMENT) {
        vinox_model_registry_destroy(registry);
        return 6;
    }
    if (strstr(vinox_serving_last_error(), "model_id") == NULL) {
        vinox_model_registry_destroy(registry);
        return 7;
    }

    // 5. Minimum vinox_model_info struct_size (up to local_path)
    info.struct_size = VINOX_MODEL_INFO_MIN_SIZE;
    if (vinox_model_registry_get_info(registry, 0, &info) != VINOX_STATUS_OK) {
        vinox_model_registry_destroy(registry);
        return 8;
    }
    if (strcmp(info.model_id, "qwen/qwen2.5-1b-instruct") != 0) {
        vinox_model_registry_destroy(registry);
        return 9;
    }

    // Store pointer to test pointer stability after registering model 2
    const char* stable_id_ptr = info.model_id;

    // 6. Register valid manifest 2 (triggering container growth)
    if (vinox_model_registry_register_manifest(registry, "test_manifest_valid2.json") != VINOX_STATUS_OK) {
        vinox_model_registry_destroy(registry);
        return 10;
    }
    if (vinox_model_registry_get_count(registry, &count) != VINOX_STATUS_OK || count != 2) {
        vinox_model_registry_destroy(registry);
        return 11;
    }

    // 7. Verify pointer stability of original string pointer after registry growth
    if (strcmp(stable_id_ptr, "qwen/qwen2.5-1b-instruct") != 0) {
        vinox_model_registry_destroy(registry);
        return 12;
    }

    // 8. Future tail-extended struct_size (+64 bytes)
    info.struct_size = (uint32_t)(sizeof(vinox_model_info) + 64);
    if (vinox_model_registry_get_info(registry, 1, &info) != VINOX_STATUS_OK) {
        vinox_model_registry_destroy(registry);
        return 13;
    }
    if (strcmp(info.model_id, "meta/llama-3.2-1b") != 0 || info.context_length != 131072) {
        vinox_model_registry_destroy(registry);
        return 14;
    }

    vinox_model_registry_destroy(registry);
    remove("test_manifest_valid1.json");
    remove("test_manifest_valid2.json");
    remove("test_manifest_bad_json.json");
    remove("test_manifest_missing_id.json");
    return 0;
}
