#pragma once
#include <cstdint>
#include "musical_composition/region_manager/iaudio_region_source_manager.h"

namespace composition {

namespace SourceManagerOps {
    constexpr uint32_t REGISTER_SOURCE = 0;
}

struct SourceManagerPayload {
    AudioSourceDescriptor descriptor;
    char filePath[200];
};

static_assert(sizeof(SourceManagerPayload) <= 256, "SourceManagerPayload exceeds delta buffer");

} // namespace composition
