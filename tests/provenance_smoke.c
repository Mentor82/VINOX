#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vinox/vinox.h"

int main(void) {
    vinox_provenance_meta source_meta = {0};
    vinox_provenance_meta derived_meta = {0};

    // 1. Min size check
    source_meta.struct_size = sizeof(source_meta);
    if (source_meta.struct_size < VINOX_PROVENANCE_META_MIN_SIZE) {
        return 1;
    }

    // 2. Source literal meta initialization
    source_meta.kind = VINOX_PROVENANCE_SOURCE_LITERAL;
    source_meta.source_id = "VINOX";
    source_meta.timestamp_ms = 1000;

    const char* literal_original = "PROJECT-X9";
    const char* literal_transported = "PROJECT-X9";
    const char* url_original = "https://github.com/Mentor82/VINOX.git";
    const char* url_transported = "https://github.com/Mentor82/VINOX.git";

    // 3. Byte-for-byte identity check (zero implicit truncation)
    if (strcmp(literal_original, literal_transported) != 0) {
        return 2;
    }
    if (strcmp(url_original, url_transported) != 0) {
        return 3;
    }

    // 4. Derived context must be flagged DERIVED_CONTEXT and must not overwrite source
    derived_meta.struct_size = sizeof(derived_meta);
    derived_meta.kind = VINOX_PROVENANCE_DERIVED_CONTEXT;
    derived_meta.source_id = "derived_summary";
    derived_meta.timestamp_ms = 2000;

    if (source_meta.kind != VINOX_PROVENANCE_SOURCE_LITERAL) {
        return 4;
    }
    if (derived_meta.kind != VINOX_PROVENANCE_DERIVED_CONTEXT) {
        return 5;
    }

    return 0;
}
