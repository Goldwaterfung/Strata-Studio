#pragma once

#include "common/system_primitives.h"
#include "Media management/codecs/icodec_reader.h"
#include <functional>
#include <memory>
#include <string>

namespace MediaManagement {

/**
 * @brief High-performance BPM analysis service using SoundTouch.
 * Designed to run on background worker threads (Layer 6).
 */
class BPMAnalyzer {
public:
    struct Result {
        float bpm = 0.0f;           ///< Detected BPM, 0.0f if failed.
        float confidence = 0.0f;    ///< Confidence score (0.0 - 1.0).
        MediaID sourceId = MediaID::invalid();       ///< Associated media identifier.
    };

    using ProgressCallback = void(*)(void* context, float progress);

    /**
     * @brief Analyze an audio source for its tempo.
     * 
     * @param sourceId Unique ID for the media.
     * @param reader Streaming reader for the audio data.
     * @param progressCallback Callback for analysis progress (0.0 - 1.0).
     * @param context User context for the callback.
     * @return Result of the analysis.
     */
    static Result analyze(
        MediaID sourceId,
        ICodecReader* reader,
        ProgressCallback progressCallback = nullptr,
        void* context = nullptr
    );
};

} // namespace MediaManagement
