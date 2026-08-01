#pragma once
#include "musical_composition/musical_primitives.h"
#include <vector>

namespace composition {

struct Take {
    TakeID takeId;
    RegionID regionId;           
    uint32_t laneIndex;          
    bool isSelected;             
};

struct TakeLane {
    LaneID laneId;
    uint32_t laneIndex;
    uint64_t startPosition;      
    uint64_t length;             
    std::vector<TakeID> takes;   
};

} // namespace composition
