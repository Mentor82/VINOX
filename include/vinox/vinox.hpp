#ifndef VINOX_VINOX_HPP
#define VINOX_VINOX_HPP

#include <cstdint>
#include <stdexcept>
#include <string_view>

#include "vinox/vinox.h"

namespace vinox {

struct Version {
    std::uint32_t abi;
    std::uint32_t major;
    std::uint32_t minor;
    std::uint32_t patch;
    std::string_view text;
};

inline Version version() {
    vinox_version_info info{};
    info.struct_size = sizeof(info);
    if (vinox_get_version(&info) != VINOX_STATUS_OK) {
        throw std::runtime_error("Failed to query vinox version");
    }
    return {
        info.abi_version,
        info.major,
        info.minor,
        info.patch,
        info.version_string,
    };
}

}  // namespace vinox

#endif
