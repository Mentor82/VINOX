#include "vinox/vinox.h"

vinox_status vinox_get_version(vinox_version_info* version_info) {
    if (version_info == nullptr) {
        return VINOX_STATUS_INVALID_ARGUMENT;
    }
    if (version_info->struct_size != sizeof(vinox_version_info)) {
        return VINOX_STATUS_INCOMPATIBLE_ABI;
    }

    version_info->abi_version = VINOX_ABI_VERSION;
    version_info->major = VINOX_VERSION_MAJOR;
    version_info->minor = VINOX_VERSION_MINOR;
    version_info->patch = VINOX_VERSION_PATCH;
    version_info->version_string = VINOX_VERSION_STRING;
    return VINOX_STATUS_OK;
}
