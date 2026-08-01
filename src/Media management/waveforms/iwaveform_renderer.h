#pragma once

#include "ihierarchical_waveform.h"
#include <memory>
#include <vector>
#include <unordered_map>

namespace Layer2 { class IStringRegistry; }

namespace MediaManagement {

class IMediaRegistry;
class ICodecFactory;

/**
 * @brief Interface for the Waveform Renderer service.
 * 
 * Provides asynchronous peak generation and synchronous retrieval of min/max data.
 * The renderer manages an internal multi-resolution cache of raw peaks.
 */
class IWaveformRenderer {
public:
    virtual ~IWaveformRenderer() = default;

    /**
     * @brief Create a hierarchical view for an asset.
     */
    virtual std::unique_ptr<IHierarchicalWaveform> getHierarchicalWaveform(MediaID mediaId) = 0;

    /**
     * @brief Ensure peak data for a specific asset and resolution is being generated.
     * 
     * @param mediaId The asset to process.
     * @param resolution The desired level of detail.
     * @return WaveformHandle to the cache slot (may be currently empty).
     */
    virtual WaveformHandle getWaveform(MediaID mediaId, WaveformResolution resolution) = 0;

    /**
     * @brief Retrieve peak and RMS data from the cache.
     * 
     * @param handle The handle returned by getWaveform.
     * @param outPeakData Array of MinMaxPair to fill (optional, can be nullptr).
     * @param outRMSData Array of float to fill (optional, can be nullptr).
     * @param startFrame Frame offset within the peak data.
     * @param numFrames Number of peak frames to retrieve.
     * @return Actual number of frames copied. 0 if handle is invalid or data not ready.
     * @thread_safety Thread-safe (read-only access).
     */
    virtual uint32_t getWaveformData(WaveformHandle handle, 
                                     MinMaxPair* outPeakData, 
                                     float* outRMSData,
                                     uint32_t startFrame, 
                                     uint32_t numFrames) const = 0;

    /**
     * @brief Check if a waveform is fully generated.
     */
    virtual bool isWaveformReady(WaveformHandle handle) const = 0;

    /**
     * @brief Get the total number of peak pairs available at a specific resolution.
     */
    virtual uint32_t getWaveformDataSize(WaveformHandle handle) const = 0;

    /**
     * @brief Check if a handle is still valid (matches current generation).
     */
    virtual bool isHandleValid(WaveformHandle handle) const = 0;

    /**
     * @brief Release interest in a waveform handle. 
     * 
     * Allows the renderer to evict data from cache when no longer needed.
     */
    virtual void releaseWaveform(WaveformHandle handle) = 0;

    /**
     * @brief Evict a specific asset from the cache entirely.
     */
    virtual void invalidateCache(MediaID mediaId) = 0;

    /**
     * @brief Factory for a streaming waveform sink.
     */
    virtual std::unique_ptr<class IWaveformSink> createSink(uint32_t sampleRate, uint16_t numChannels) = 0;

    /**
     * @brief Imports data from a completed sink.
     */
    virtual void importWaveformData(MediaID mediaId, 
                                   const std::unordered_map<WaveformResolution, std::vector<MinMaxPair>>& peaks, 
                                   const std::unordered_map<WaveformResolution, std::vector<float>>& rms) = 0;

    /**
     * @brief Service tick called from Main Thread to process job completion.
     */
    virtual void update() = 0;

    /**
     * @brief Factory method.
     */
    static std::unique_ptr<IWaveformRenderer> create(IMediaRegistry* registry, 
                                                     Layer2::IStringRegistry* strings,
                                                     ICodecFactory* codecFactory);
};

} // namespace MediaManagement
