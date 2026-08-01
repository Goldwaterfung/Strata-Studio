#pragma once
#include <cstdint>

namespace composition {

namespace CompingOps {
    constexpr uint32_t CREATE_LANE = 0;
    constexpr uint32_t CREATE_TAKE = 1;
    constexpr uint32_t SELECT_TAKE = 2;
    constexpr uint32_t REMOVE_TAKE = 3;
}

struct TakePayload {
    uint32_t laneId;
    uint32_t regionId;
    bool isSelected;
};

struct SelectTakePayload {
    uint32_t takeId;
    uint32_t prevTakeId;
};

struct CreateLanePayload {
    uint32_t laneId;
    uint64_t positionSample;
    uint64_t lengthSamples;
};

} // namespace composition
