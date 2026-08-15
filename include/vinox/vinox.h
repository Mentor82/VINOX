#ifndef VINOX_VINOX_H
#define VINOX_VINOX_H

#include <stdint.h>

#include "vinox/export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VINOX_ABI_VERSION 1u

typedef enum vinox_status {
    VINOX_STATUS_OK = 0,
    VINOX_STATUS_INVALID_ARGUMENT = 1,
    VINOX_STATUS_INCOMPATIBLE_ABI = 2,
    VINOX_STATUS_RUNTIME_ERROR = 3,
    VINOX_STATUS_CANCELLED = 4
} vinox_status;

typedef struct vinox_version_info {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    const char* version_string;
} vinox_version_info;

VINOX_API vinox_status vinox_get_version(vinox_version_info* version_info);

#ifdef __cplusplus
}
#endif

#endif
