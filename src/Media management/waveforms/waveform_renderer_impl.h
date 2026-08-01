#pragma once

#include "iwaveform_renderer.h"
#include "Media management/registry/imedia_registry.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Media management/codecs/icodec_factory.h"
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <deque>

namespace MediaManagement {

class WaveformRendererImpl : public IWaveformRenderer {
public:
    WaveformRendererImpl(IMediaRegistry* registry, 
                         Layer2::IStringRegistry* strings,
                         ICodecFactory* codecFactory);
    ~WaveformRendererImpl() override;

    std::unique_ptr<IHierarchicalWaveform> getHierarchicalWaveform(MediaID mediaId) override;

    WaveformHandle getWaveform(MediaID mediaId, WaveformResolution resolution) override;
    uint32_t getWaveformData(WaveformHandle handle, 
                             MinMaxPair* outPeakData, 
                             float* outRMSData,
                             uint32_t startFrame, 
                             uint32_t numFrames) const override;
    bool isWaveformReady(WaveformHandle handle) const override;
    uint32_t getWaveformDataSize(WaveformHandle handle) const override;
    bool isHandleValid(WaveformHandle handle) const override;
    void releaseWaveform(WaveformHandle handle) override;
    void invalidateCache(MediaID mediaId) override;
    void update() override;

    std::unique_ptr<class IWaveformSink> createSink(uint32_t sampleRate, uint16_t numChannels) override;

    /**
     * @brief Imports data from a completed sink.
     */
    void importWaveformData(MediaID mediaId, const std::unordered_map<WaveformResolution, std::vector<MinMaxPair>>& peaks, const std::unordered_map<WaveformResolution, std::vector<float>>& rms) override;

    /**
     * @brief High-performance lock-consolidated direct peaks lookup bypassing handles.
     */
    uint32_t getPeaksDirect(MediaID mediaId,
                            uint64_t startSample,
                            uint64_t endSample,
                            float samplesPerPixel,
                            MinMaxPair* outPeaks,
                            uint32_t bufferSize) const;

    /**
     * @brief Check if a specific media waveform resolution is ready without acquiring a handle.
     */
    bool isResolutionReady(MediaID mediaId, WaveformResolution resolution) const;

private:
    struct CacheEntry {
        MediaID mediaId;
        WaveformResolution resolution;
        uint32_t generation;
        std::vector<MinMaxPair> peaks;
        std::vector<float> rms;
        std::atomic<bool> ready{false};
        std::atomic<bool> cancelled{false};
        uint32_t useCount{0};
        uint64_t lastAccessTicket{0};
    };

    struct PendingJob {
        MediaID mediaId;
        WaveformResolution resolution;
        uint32_t cacheId;
    };

    void workerLoop();
    void processJob(const PendingJob& job);

    IMediaRegistry* registry_;
    Layer2::IStringRegistry* strings_;
    ICodecFactory* codecFactory_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint32_t, std::shared_ptr<CacheEntry>> cache_;
    
    // Quick lookup from (MediaID, Resolution) to cacheId
    std::unordered_map<uint64_t, uint32_t> lookup_;

    std::deque<PendingJob> jobQueue_;
    std::thread workerThread_;
    std::atomic<bool> running_{true};
    std::condition_variable_any cv_;

    std::atomic<uint32_t> nextCacheId_{1};
    mutable uint64_t accessCounter_{0};
};

} // namespace MediaManagement
