#pragma once
#include "common/system_primitives.h"
#include "musical_composition/musical_primitives.h"

namespace bridge {

// High/low decimated amplitude peaks representing a block of frames
struct MinMaxPeak {
    float minVal;
    float maxVal;
};

struct WaveformSegment {
    const MinMaxPeak* peaks;  // Pointer to decimated peaks array
    uint32_t sampleCount;     // Number of peak entries ready to be rendered
    bool isLoaded;            // True if peak file analysis is fully complete
};

/**
 * @brief Manages background decimation caches for lag-free zoom and scroll visualization
 */
class IWaveformCacheProvider {
public:
    virtual ~IWaveformCacheProvider() = default;

    // Triggers asynchronous decimation on a background butler thread
    virtual void requestWaveformLoad(MediaID mediaId) = 0;
    virtual void releaseWaveform(MediaID mediaId) = 0;

    // Returns a decimation segment fitting exactly into a pixel width
    virtual WaveformSegment getPeakDataForViewport(
        MediaID mediaId,
        uint64_t startFrame,
        uint64_t endFrame,
        uint32_t targetPixelWidth
    ) = 0;
};

} // namespace bridge
