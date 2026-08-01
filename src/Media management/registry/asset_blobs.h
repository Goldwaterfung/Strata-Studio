#pragma once

#include "common/system_primitives.h"
#include "Media management/waveforms/waveform_primitives.h"
#include <vector>
#include <unordered_map>

namespace MediaManagement {

/**
 * @brief Container for large analysis arrays that don't fit in POD AssetInfo.
 */
struct AssetAnalysisBlobs {
    std::vector<float> spectralFlux;
    std::vector<uint64_t> transientPositions;
    std::vector<float> transientAmplitudes;
    std::vector<float> pitchData;
};

/**
 * @brief Container for multi-resolution waveform data.
 */
struct AssetWaveformBlobs {
    std::unordered_map<WaveformResolution, std::vector<MinMaxPair>> peaks;
    std::unordered_map<WaveformResolution, std::vector<float>> rms;
};

} // namespace MediaManagement
