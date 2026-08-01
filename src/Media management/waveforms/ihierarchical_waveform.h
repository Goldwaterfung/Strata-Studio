#pragma once

#include "waveform_primitives.h"
#include <cstdint>

namespace MediaManagement {

/**
 * @brief An adaptive waveform view that automatically handles LOD (Level of Detail).
 */
class IHierarchicalWaveform {
public:
    virtual ~IHierarchicalWaveform() = default;

    /**
     * @brief Retrieve peaks for a specific sample range and zoom level.
     * 
     * The implementation will choose the most appropriate resolution from the 
     * hierarchy (FULL, HIGH, MEDIUM, etc.) based on samplesPerPixel.
     * 
     * @param startSample The starting sample in the original audio file.
     * @param endSample The ending sample in the original audio file.
     * @param samplesPerPixel The current horizontal zoom level (LOD).
     * @param outPeaks Buffer to receive min/max pairs.
     * @param bufferSize Number of MinMaxPair slots in outPeaks.
     * @return Number of peak pairs actually written.
     */
    virtual uint32_t getPeaks(uint64_t startSample, 
                              uint64_t endSample, 
                              float samplesPerPixel, 
                              MinMaxPair* outPeaks, 
                              uint32_t bufferSize) const = 0;

    /**
     * @brief Returns true if at least some resolutions are ready for viewing.
     */
    virtual bool isAnyReady() const = 0;

    /**
     * @brief Returns true if all resolutions are fully cached.
     */
    virtual bool isFullyReady() const = 0;

    /**
     * @brief Get the associated MediaID.
     */
    virtual MediaID getMediaId() const = 0;
};

} // namespace MediaManagement
