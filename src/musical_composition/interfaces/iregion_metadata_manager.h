#pragma once
#include "common/system_primitives.h"
#include "musical_composition/musical_primitives.h"

namespace composition {

struct RegionMetadata {
    char name[MAX_NAME_LENGTH];
    char comment[MAX_COMMENT_LENGTH];
    uint32_t colorARGB;
    bool hasComment;
};

static_assert(std::is_pod<RegionMetadata>::value, "RegionMetadata must be Plain Old Data");

class IRegionMetadataManager {
public:
    virtual ~IRegionMetadataManager() = default;

    virtual void setRegionMetadata(RegionID id, const RegionMetadata& metadata, bool pushDelta = true) = 0;
    virtual void getRegionMetadata(RegionID id, RegionMetadata& outMetadata) const = 0;
    virtual void removeRegionMetadata(RegionID id, bool pushDelta = true) = 0;
    virtual bool hasRegionMetadata(RegionID id) const = 0;
    
    virtual void clear() = 0;
};

} // namespace composition
