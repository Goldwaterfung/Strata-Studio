#pragma once

#include "Media management/registry/media_primitives.h"
#include "Media management/analysis/analysis_primitives.h"
#include "Media management/waveforms/waveform_primitives.h"
#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>

namespace Layer2 { class IStringRegistry; }

namespace MediaManagement {

class IMediaRegistry;
class ICodecFactory;
class IAudioAnalysisEngine;
class IWaveformRenderer;

/**
 * @brief Base interface for pushing audio frames into a processing module.
 */
class IAudioStreamSink {
public:
    virtual ~IAudioStreamSink() = default;

    /**
     * @brief Process a block of interleaved audio frames.
     */
    virtual void processFrames(const float* buffer, uint32_t numFrames, uint16_t numChannels) = 0;

    /**
     * @brief Signal end of stream and finalize processing.
     */
    virtual void finalize() = 0;
};

/**
 * @brief Specialized sink for audio analysis results.
 */
class IAnalysisSink : public IAudioStreamSink {
public:
    virtual AnalysisResult getResult() const = 0;
};

/**
 * @brief Specialized sink for multi-resolution waveform generation.
 */
class IWaveformSink : public IAudioStreamSink {
public:
    /**
     * @brief Retrieve the generated peaks for a specific resolution.
     */
    virtual void getPeaks(WaveformResolution res, std::vector<MinMaxPair>& outPeaks) const = 0;

    /**
     * @brief Retrieve all generated data (used by the intake pipeline for commit).
     */
    virtual const std::unordered_map<WaveformResolution, std::vector<MinMaxPair>>& getAllPeaks() const = 0;
    virtual const std::unordered_map<WaveformResolution, std::vector<float>>& getAllRMS() const = 0;
};

/**
 * @brief Deep module that hydrates an asset in a single file pass.
 */
class IMediaIntakePipeline {
public:
    virtual ~IMediaIntakePipeline() = default;

    struct IntakeResult {
        bool success;
        MediaID mediaId;
        std::string errorMessage;
    };

    /**
     * @brief Hydrates an asset completely in one pass.
     * Synchronous - must be called from a worker thread.
     */
    virtual IntakeResult processAsset(
        const std::string& filePath,
        const ImportOptions& options,
        std::function<void(float)> progressCallback
    ) = 0;

    /**
     * @brief Factory method.
     */
    static std::unique_ptr<IMediaIntakePipeline> create(
        IMediaRegistry* registry,
        Layer2::IStringRegistry* strings,
        ICodecFactory* codecs,
        IAudioAnalysisEngine* analysis,
        IWaveformRenderer* waveforms
    );
};

} // namespace MediaManagement
