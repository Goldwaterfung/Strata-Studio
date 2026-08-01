// src/Middle Bridge/waveform_cache_provider.h
#pragma once

#include "Middle Bridge/telemetry/iwaveform_cache_provider.h"
#include "Media management/waveforms/iwaveform_renderer.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>

namespace bridge {

class WaveformCacheProvider : public IWaveformCacheProvider {
public:
    explicit WaveformCacheProvider(MediaManagement::IWaveformRenderer* waveformRenderer);
    ~WaveformCacheProvider() override;

    // Triggers asynchronous decimation on a background butler thread
    void requestWaveformLoad(MediaID mediaId) override;
    void releaseWaveform(MediaID mediaId) override;

    // Returns a decimation segment fitting exactly into a pixel width
    WaveformSegment getPeakDataForViewport(
        MediaID mediaId,
        uint64_t startFrame,
        uint64_t endFrame,
        uint32_t targetPixelWidth
    ) override;

private:
    struct CacheEntry {
        std::unique_ptr<MediaManagement::IHierarchicalWaveform> hierarchicalWaveform;
        std::vector<MediaManagement::WaveformHandle> activeHandles;
        std::vector<MinMaxPeak> resampledPeaks;
        std::vector<MediaManagement::MinMaxPair> tempPeaksScratch;
    };

    struct MediaIdHash {
        std::size_t operator()(const MediaID& m) const {
            return std::hash<uint64_t>{}(m.toRaw());
        }
    };

    struct MediaIdEqual {
        bool operator()(const MediaID& a, const MediaID& b) const {
            return a == b;
        }
    };

    MediaManagement::IWaveformRenderer* waveformRenderer_ = nullptr;
    mutable std::mutex mutex_;
    std::unordered_map<MediaID, std::unique_ptr<CacheEntry>, MediaIdHash, MediaIdEqual> loadedWaveforms_;
};

} // namespace bridge
