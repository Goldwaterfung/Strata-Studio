#pragma once
#include <cstdint>
#include "common/system_primitives.h"

namespace composition {

namespace RegionMetadataOps {
    constexpr uint32_t SET_METADATA    = 0;
    constexpr uint32_t REMOVE_METADATA = 1;
}

struct RegionMetadataPayload {
    RegionID regionId;
    char name[MAX_NAME_LENGTH];
    char comment[MAX_COMMENT_LENGTH];
    uint32_t colorARGB;
    bool hasComment;
    uint8_t reserved[3]; // Alignment padding (8 + 64 + 1024 + 4 + 1 + 3 = 1104 bytes)
};

static_assert(sizeof(RegionMetadataPayload) <= 1120, "Payload structure alignment");

} // namespace composition
