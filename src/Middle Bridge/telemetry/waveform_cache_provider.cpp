// src/Middle Bridge/waveform_cache_provider.cpp
#include "Middle Bridge/telemetry/waveform_cache_provider.h"
#include <algorithm>

namespace bridge {

WaveformCacheProvider::WaveformCacheProvider(MediaManagement::IWaveformRenderer* waveformRenderer)
    : waveformRenderer_(waveformRenderer) {}

WaveformCacheProvider::~WaveformCacheProvider() {
    std::lock_guard lock(mutex_);
    // Release all held waveform handles
    for (auto& [mediaId, entry] : loadedWaveforms_) {
        if (waveformRenderer_) {
            for (auto h : entry->activeHandles) {
                waveformRenderer_->releaseWaveform(h);
            }
        }
    }
}

void WaveformCacheProvider::requestWaveformLoad(MediaID mediaId) {
    if (!waveformRenderer_ || !mediaId.isValid()) return;

    std::lock_guard lock(mutex_);
    auto it = loadedWaveforms_.find(mediaId);
    if (it != loadedWaveforms_.end()) return;

    auto entry = std::make_unique<CacheEntry>();
    entry->hierarchicalWaveform = waveformRenderer_->getHierarchicalWaveform(mediaId);

    // Warm up the resolutions in the background and keep handles to prevent cache eviction
    entry->activeHandles.push_back(waveformRenderer_->getWaveform(mediaId, MediaManagement::WaveformResolution::FULL));
    entry->activeHandles.push_back(waveformRenderer_->getWaveform(mediaId, MediaManagement::WaveformResolution::HIGH));
    entry->activeHandles.push_back(waveformRenderer_->getWaveform(mediaId, MediaManagement::WaveformResolution::MEDIUM));
    entry->activeHandles.push_back(waveformRenderer_->getWaveform(mediaId, MediaManagement::WaveformResolution::LOW));
    entry->activeHandles.push_back(waveformRenderer_->getWaveform(mediaId, MediaManagement::WaveformResolution::OVERVIEW));

    loadedWaveforms_[mediaId] = std::move(entry);
}

void WaveformCacheProvider::releaseWaveform(MediaID mediaId) {
    std::lock_guard lock(mutex_);
    auto it = loadedWaveforms_.find(mediaId);
    if (it == loadedWaveforms_.end()) return;

    if (waveformRenderer_) {
        for (auto h : it->second->activeHandles) {
            waveformRenderer_->releaseWaveform(h);
        }
    }
    loadedWaveforms_.erase(it);
}

WaveformSegment WaveformCacheProvider::getPeakDataForViewport(
    MediaID mediaId,
    uint64_t startFrame,
    uint64_t endFrame,
    uint32_t targetPixelWidth
) {
    std::lock_guard lock(mutex_);
    auto it = loadedWaveforms_.find(mediaId);
    if (it == loadedWaveforms_.end() || targetPixelWidth == 0 || endFrame <= startFrame) {
        return { nullptr, 0, false };
    }

    auto& entry = it->second;
    if (!entry->hierarchicalWaveform) {
        return { nullptr, 0, false };
    }

    // Only resize if targetPixelWidth has grown or is different
    if (entry->resampledPeaks.size() != targetPixelWidth) {
        entry->resampledPeaks.resize(targetPixelWidth);
    }
    if (entry->tempPeaksScratch.size() != targetPixelWidth) {
        entry->tempPeaksScratch.resize(targetPixelWidth);
    }

    float samplesPerPixel = static_cast<float>(endFrame - startFrame) / static_cast<float>(targetPixelWidth);

    uint32_t count = entry->hierarchicalWaveform->getPeaks(
        startFrame,
        endFrame,
        samplesPerPixel,
        entry->tempPeaksScratch.data(),
        targetPixelWidth
    );

    // Safeguard out-of-bounds access in case count exceeds targetPixelWidth
    count = std::min(count, targetPixelWidth);

    for (uint32_t i = 0; i < count; ++i) {
        entry->resampledPeaks[i].minVal = entry->tempPeaksScratch[i].min;
        entry->resampledPeaks[i].maxVal = entry->tempPeaksScratch[i].max;
    }

    // Zero out any unfilled pixel slots
    for (uint32_t i = count; i < targetPixelWidth; ++i) {
        entry->resampledPeaks[i].minVal = 0.0f;
        entry->resampledPeaks[i].maxVal = 0.0f;
    }

    bool isLoaded = entry->hierarchicalWaveform->isFullyReady();
    return { entry->resampledPeaks.data(), targetPixelWidth, isLoaded };
}

} // namespace bridge
