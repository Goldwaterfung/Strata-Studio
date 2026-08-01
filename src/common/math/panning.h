#pragma once

#include <cmath>
#include <algorithm>
#include "primitives.h"

namespace Math {

/**
 * @brief Equal Power Panning utilities.
 */
namespace Panning {

    /**
     * @brief Equal power panning (sine/cosine law) with -3dB compensation.
     * Maintains steady perceived volume across the stereo field.
     * Center (0.5) = 1.0 (0dB), Hard Left/Right = 1.414 (+3dB).
     */
    inline void calculateEqualPower(float pan, float& left, float& right) {
        pan = std::clamp(pan, 0.0f, 1.0f);
        float angle = pan * Constants::HALF_PI;
        constexpr float COMPENSATE_3DB = 1.41421356f; // sqrt(2)
        left = std::cos(angle) * COMPENSATE_3DB;
        right = std::sin(angle) * COMPENSATE_3DB;
    }

    /**
     * @brief Linear balance panning (stereo balance control).
     * Maintains 1.0 (0dB) gain at center (0.5), and attenuates the opposite channel.
     */
    inline void calculateLinear(float pan, float& left, float& right) {
        pan = std::clamp(pan, 0.0f, 1.0f);
        if (pan < 0.5f) {
            left = 1.0f;
            right = pan * 2.0f;
        } else {
            left = (1.0f - pan) * 2.0f;
            right = 1.0f;
        }
    }

} // namespace Panning

} // namespace Math
