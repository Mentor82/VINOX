#include <stddef.h>

#include "vinox/vinox.h"

int main(void) {
    vinox_version_info version = {0};

    if (vinox_get_version(NULL) != VINOX_STATUS_INVALID_ARGUMENT) {
        return 1;
    }
    if (vinox_get_version(&version) != VINOX_STATUS_INCOMPATIBLE_ABI) {
        return 2;
    }

    version.struct_size = (uint32_t)sizeof(version);

    if (vinox_get_version(&version) != VINOX_STATUS_OK) {
        return 3;
    }
    if (version.abi_version != VINOX_ABI_VERSION) {
        return 4;
    }
    if (version.version_string == NULL) {
        return 5;
    }
    return 0;
}