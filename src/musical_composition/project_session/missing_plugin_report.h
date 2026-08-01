#pragma once

#include "system_primitives.h"
#include <string>

namespace composition {

struct MissingPluginReport {
    TrackID trackId;
    int slotIndex; // -1 for instrument, 0-7 for inserts
    uint32_t requestedPluginId;
    std::string originalName;
};

} // namespace composition
