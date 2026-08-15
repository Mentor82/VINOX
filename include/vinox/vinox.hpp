#ifndef VINOX_VINOX_HPP
#define VINOX_VINOX_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
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

struct ProvenanceMeta {
    vinox_provenance_kind kind{VINOX_PROVENANCE_SOURCE_LITERAL};
    std::string source_id;
    uint64_t timestamp_ms{0};
};

/**
 * @brief Asserts exact byte-for-byte identity of opaque literal identifiers across module boundaries.
 * Guarantees that no implicit truncation or normalization occurred.
 */
inline bool is_literal_identical(std::string_view original, std::string_view candidate) noexcept {
    return original == candidate;
}

}  // namespace vinox

#endif
