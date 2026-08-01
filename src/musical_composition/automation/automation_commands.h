#pragma once
#include "musical_composition/musical_primitives.h"

namespace composition {

namespace AutomationOps {
    constexpr uint32_t ADD_POINT = 0;
    constexpr uint32_t REMOVE_POINT = 1;
    constexpr uint32_t UPDATE_POINT = 2;
    constexpr uint32_t CLEAR_LANE = 3;
    constexpr uint32_t CREATE_LANE = 4;
    constexpr uint32_t REMOVE_LANE = 5;
}

struct AutomationLanePayload {
    AutomationTarget target;
};

struct AutomationPointPayload {
    AutomationTarget target;
    uint64_t samplePosition;
    float value;
    uint8_t curveShape;
    float tension;
};

} // namespace composition
