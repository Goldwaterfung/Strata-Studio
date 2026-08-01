#pragma once

#include <cmath>
#include <algorithm>

namespace Math {

/**
 * @brief Utilities for gain and decibel conversions.
 */
namespace Gain {

    /**
     * @brief Normalized automation value representing 0.0dB (Unity Gain).
     * Based on the audio taper mapping: value = sqrt(1.0 / 2.0).
     */
    static constexpr float UNITY_NORMALIZED = 0.70710678f;

    /**
     * @brief Normalized automation value representing center pan.
     */
    static constexpr float CENTER_PAN_NORMALIZED = 0.5f;

    /**
     * @brief Converts decibels to a linear gain coefficient.
     * Formula: 10^(dB / 20)
     * 
     * @param dB The value in decibels.
     * @return The linear gain coefficient.
     */
    inline float dBToCoeff(float dB) {
        if (dB <= -144.0f) {
            return 0.0f;
        }
        return std::pow(10.0f, dB * 0.05f);
    }

    /**
     * @brief Converts a linear gain coefficient to decibels.
     * Formula: 20 * log10(coefficient)
     * 
     * @param coeff The linear gain coefficient.
     * @return The value in decibels.
     */
    inline float coeffTodB(float coeff) {
        if (coeff <= 0.000000001f) { // -180dB floor
            return -180.0f;
        }
        return 20.0f * std::log10(coeff);
    }

    /**
     * @brief Clamps a coefficient to a safe range [0, 2] (max +6dB).
     * Professional DAWs typically limit fader gain to prevent clipping.
     */
    inline float clampCoeff(float coeff, float maxCoeff = 2.0f) {
        return std::max(0.0f, std::min(coeff, maxCoeff));
    }

    /**
     * @brief Maps a normalized UI/Automation value [0.0, 1.0] to a linear gain coefficient [0.0, 2.0].
     * Uses an audio taper (square law): gain = value^2 * 2.0.
     * This exactly maps: 0.0 -> -inf, 0.5 -> -6dB, 0.707 -> 0dB, 1.0 -> +6dB.
     */
    inline float normalizedToLinear(float norm) {
        float clamped = std::max(0.0f, std::min(norm, 1.0f));
        return clamped * clamped * 2.0f;
    }

    /**
     * @brief Maps a linear gain coefficient [0.0, 2.0] back to a normalized UI/Automation value [0.0, 1.0].
     * Inverse of normalizedToLinear: value = sqrt(gain / 2.0).
     */
    inline float linearToNormalized(float gain) {
        float clamped = std::max(0.0f, std::min(gain, 2.0f));
        return std::sqrt(clamped * 0.5f);
    }

} // namespace Gain

} // namespace Math
