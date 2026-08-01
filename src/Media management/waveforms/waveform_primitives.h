#pragma once

#include "common/system_primitives.h"
#include <cstdint>

namespace MediaManagement {

/**
 * @brief Resolution levels for waveform peak generation.
 */
enum class WaveformResolution : uint8_t {
    FULL = 0,       ///< 1:1 (every sample)
    HIGH = 1,       ///< 1:64
    MEDIUM = 2,     ///< 1:256
    LOW = 3,        ///< 1:1024
    OVERVIEW = 4    ///< 1:4096
};

/**
 * @brief Get the decimation ratio for a given waveform resolution level.
 */
constexpr inline uint32_t getWaveformResolutionRatio(WaveformResolution res) {
    switch (res) {
        case WaveformResolution::FULL:     return 1;
        case WaveformResolution::HIGH:     return 64;
        case WaveformResolution::MEDIUM:   return 256;
        case WaveformResolution::LOW:      return 1024;
        case WaveformResolution::OVERVIEW: return 4096;
        default: return 256;
    }
}

/**
 * @brief Pair of min and max peak values (TRUE POD).
 */
struct MinMaxPair {
    float min;
    float max;
};

static_assert(std::is_pod<MinMaxPair>::value, "MinMaxPair must be Plain Old Data");

/**
 * @brief Generation-counted handle for waveform cache access.
 */
struct WaveformHandle {
    uint32_t cacheId;
    uint32_t generation;

    constexpr bool isValid() const { return cacheId != UINT32_MAX && generation != 0; }
    static constexpr WaveformHandle invalid() { return { UINT32_MAX, 0 }; }

    bool operator==(const WaveformHandle& other) const {
        return cacheId == other.cacheId && generation == other.generation;
    }
};

static_assert(std::is_pod<WaveformHandle>::value, "WaveformHandle must be Plain Old Data");

} // namespace MediaManagement
