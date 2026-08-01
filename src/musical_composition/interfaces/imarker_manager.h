#pragma once
#include <cstdint>
#include "common/system_primitives.h"

namespace composition {

struct MarkerInfo {
    MarkerUUID uuid;
    uint64_t framePosition;
    char label[MAX_NAME_LENGTH];
    uint32_t colorARGB;
    uint32_t markerNumber;
};

static_assert(std::is_pod<MarkerInfo>::value, "MarkerInfo must be Plain Old Data");

class IMarkerManager {
public:
    virtual ~IMarkerManager() = default;

    virtual MarkerUUID addMarker(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB, bool pushDelta = true) = 0;
    virtual void removeMarker(const MarkerUUID& uuid, bool pushDelta = true) = 0;
    virtual void updateMarker(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB, bool pushDelta = true) = 0;
    virtual uint32_t getMarkersInRange(uint64_t startFrame, uint64_t endFrame,
                                       MarkerInfo* outMarkers, uint32_t maxCount) const = 0;
    virtual void clear() = 0;
};

} // namespace composition
