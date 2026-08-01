#pragma once

#include "common/system_primitives.h"
#include <cstdint>

namespace MediaManagement {

/**
 * @brief Detailed analysis result for a media asset.
 * 
 * This structure is a TRUE POD and must match Section 4.3 of the 
 * Layer 6 Media Management Analysis Specification.
 */
struct AnalysisResult {
    float peakDecibels;
    float rmsDecibels;
    float dynamicRange;
    float spectralCentroid;
    float spectralSpread;
    float spectralSkewness;
    float spectralKurtosis;
    float integratedLUFS;
    float momentaryLUFS;
    float shortTermLUFS;
    float loudnessRange;
    float truePeakdB;
    uint32_t spectralFluxCount;
    uint32_t transientCount;
    uint32_t pitchFrameCount;
    float tempo;
    float tempoConfidence;
    uint64_t tempoPositionSample;
    uint8_t keyRoot;
    bool isMinor;
    float keyConfidence;
    uint64_t keyPositionSample;
};

static_assert(std::is_pod<AnalysisResult>::value, "AnalysisResult must be Plain Old Data");

} // namespace MediaManagement
