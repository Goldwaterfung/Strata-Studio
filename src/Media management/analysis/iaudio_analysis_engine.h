#pragma once

#include "analysis_primitives.h"
#include <memory>
#include <cstdint>

namespace Layer2 { class IStringRegistry; }

namespace MediaManagement {

class IMediaRegistry;

/**
 * @brief Unified interface for the Audio Analysis Engine.
 * 
 * Orchestrates multiple analysis tiers (Loudness, Spectral, Tempo) in the background.
 * Adheres to Section 4.3 of the Layer 6 Media Management Analysis Specification.
 */
class IAudioAnalysisEngine {
public:
    virtual ~IAudioAnalysisEngine() = default;

    /**
     * @brief Signature for analysis completion callback.
     * Always invoked on the Main Thread.
     */
    using CompletionCallback = void(*)(void* context, const AnalysisResult& result);

    /**
     * @brief Tier 1: Real-time analysis (RT-Safe).
     * Must be called from the processing thread. No allocations.
     */
    virtual void analyzeRealtime(const struct AudioBuffer* input,
                                 uint32_t numSamples,
                                 AnalysisResult& outResult) = 0;

    /**
     * @brief Tier 2: Synchronous full analysis.
     * Blocks until analysis is complete.
     */
    virtual bool analyze(MediaID mediaId, AnalysisResult& result) = 0;

    /**
     * @brief Tier 2: Asynchronous full analysis.
     * Queues a job for background processing.
     */
    virtual bool analyzeAsync(MediaID mediaId, 
                             CompletionCallback callback,
                             void* context) = 0;

    /**
     * @brief Data Query: Retrieve spectral flux array.
     */
    virtual void getSpectralFluxData(MediaID mediaId, float* buffer, uint32_t bufferSize) = 0;

    /**
     * @brief Data Query: Retrieve transient positions and amplitudes.
     */
    virtual void getTransientData(MediaID mediaId, uint64_t* positions, float* amplitudes, uint32_t bufferSize) = 0;

    /**
     * @brief Data Query: Retrieve pitch contour data.
     */
    virtual void getPitchData(MediaID mediaId, float* buffer, uint32_t bufferSize) = 0;

    /**
     * @brief Specialized: Compute loudness only.
     */
    virtual void calculateLoudness(MediaID mediaId, float* integrated, float* range, float* truePeak) = 0;

    /**
     * @brief Specialized: Detect tempo only.
     */
    virtual void detectTempo(MediaID mediaId, float* tempo, float* confidence) = 0;

    /**
     * @brief Specialized: Detect musical key only.
     */
    virtual void detectKey(MediaID mediaId, uint8_t* root, bool* isMinor, float* confidence) = 0;

    /**
     * @brief Factory for a streaming analysis sink.
     */
    virtual std::unique_ptr<class IAnalysisSink> createSink(uint32_t sampleRate, uint16_t numChannels) = 0;

    /**
     * @brief Service tick called from Main Thread to process callbacks.
     */
    virtual void update() = 0;

    /**
     * @brief Factory method.
     */
    static std::unique_ptr<IAudioAnalysisEngine> create(class IMediaRegistry* registry, 
                                                        class Layer2::IStringRegistry* strings,
                                                        class ICodecFactory* codecFactory,
                                                        uint32_t fftSize = 2048);
};

} // namespace MediaManagement
