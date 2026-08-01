#include "waveform_renderer_impl.h"
#include "Media management/codecs/icodec_reader.h"
#include <algorithm>
#include <cmath>
#include "Media management/intake/imedia_intake_pipeline.h"

namespace MediaManagement {

/**
 * @brief Implementation of the adaptive hierarchical waveform view.
 */
class HierarchicalWaveformImpl : public IHierarchicalWaveform {
public:
    HierarchicalWaveformImpl(MediaID mediaId, class IWaveformRenderer* renderer)
        : mediaId_(mediaId), renderer_(renderer) {}

    MediaID getMediaId() const override { return mediaId_; }

    bool isAnyReady() const override {
        auto* impl = static_cast<const WaveformRendererImpl*>(renderer_);
        return impl->isResolutionReady(mediaId_, WaveformResolution::OVERVIEW);
    }

    bool isFullyReady() const override {
        auto* impl = static_cast<const WaveformRendererImpl*>(renderer_);
        return impl->isResolutionReady(mediaId_, WaveformResolution::OVERVIEW) &&
               impl->isResolutionReady(mediaId_, WaveformResolution::HIGH);
    }

    uint32_t getPeaks(uint64_t startSample, 
                       uint64_t endSample, 
                       float samplesPerPixel, 
                       MinMaxPair* outPeaks, 
                       uint32_t bufferSize) const override {
        auto* impl = static_cast<const WaveformRendererImpl*>(renderer_);
        return impl->getPeaksDirect(mediaId_, startSample, endSample, samplesPerPixel, outPeaks, bufferSize);
    }

private:
    MediaID mediaId_;
    class IWaveformRenderer* renderer_;
};

WaveformRendererImpl::WaveformRendererImpl(IMediaRegistry* registry, 
                                           Layer2::IStringRegistry* strings,
                                           ICodecFactory* codecFactory)
    : registry_(registry), strings_(strings), codecFactory_(codecFactory)
{
    workerThread_ = std::thread(&WaveformRendererImpl::workerLoop, this);
}

WaveformRendererImpl::~WaveformRendererImpl() {
    running_ = false;
    cv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

std::unique_ptr<IHierarchicalWaveform> WaveformRendererImpl::getHierarchicalWaveform(MediaID mediaId) {
    return std::make_unique<HierarchicalWaveformImpl>(mediaId, this);
}

WaveformHandle WaveformRendererImpl::getWaveform(MediaID mediaId, WaveformResolution resolution) {
    std::unique_lock lock(mutex_);
    
    uint64_t key = (static_cast<uint64_t>(mediaId.toRaw()) << 8) | static_cast<uint8_t>(resolution);
    
    auto it = lookup_.find(key);
    if (it != lookup_.end()) {
        auto entryIt = cache_.find(it->second);
        if (entryIt != cache_.end()) {
            entryIt->second->useCount++;
            entryIt->second->lastAccessTicket = ++accessCounter_;
            return { entryIt->first, entryIt->second->generation };
        }
    }
    
    // Evict oldest unused entry if cache grows too large
    if (cache_.size() >= 100) {
        uint32_t oldestCacheId = 0;
        uint64_t oldestTicket = std::numeric_limits<uint64_t>::max();
        for (const auto& [id, cEntry] : cache_) {
            if (cEntry->useCount == 0 && cEntry->lastAccessTicket < oldestTicket) {
                oldestTicket = cEntry->lastAccessTicket;
                oldestCacheId = id;
            }
        }
        if (oldestCacheId != 0) {
            auto evictEntry = cache_[oldestCacheId];
            uint64_t evictKey = (static_cast<uint64_t>(evictEntry->mediaId.toRaw()) << 8) | static_cast<uint8_t>(evictEntry->resolution);
            lookup_.erase(evictKey);
            evictEntry->cancelled = true;
            cache_.erase(oldestCacheId);
        }
    }

    // Check Registry first for data locality (Phase 2)
    AssetWaveformBlobs blobs;
    if (registry_->getWaveformBlobs(mediaId, blobs)) {
        auto peakIt = blobs.peaks.find(resolution);
        if (peakIt != blobs.peaks.end()) {
            uint32_t cacheId = nextCacheId_++;
            auto entry = std::make_shared<CacheEntry>();
            entry->mediaId = mediaId;
            entry->resolution = resolution;
            entry->generation = 1;
            entry->useCount = 1;
            entry->peaks = peakIt->second;
            entry->lastAccessTicket = ++accessCounter_;
            
            auto rmsIt = blobs.rms.find(resolution);
            if (rmsIt != blobs.rms.end()) entry->rms = rmsIt->second;
            
            entry->ready = true;
            cache_[cacheId] = entry;
            lookup_[key] = cacheId;
            return { cacheId, entry->generation };
        }
    }

    // Create new entry and queue job
    uint32_t cacheId = nextCacheId_++;
    auto entry = std::make_shared<CacheEntry>();
    entry->mediaId = mediaId;
    entry->resolution = resolution;
    entry->generation = 1;
    entry->useCount = 1;
    entry->lastAccessTicket = ++accessCounter_;
    
    cache_[cacheId] = entry;
    lookup_[key] = cacheId;
    
    jobQueue_.push_back({ mediaId, resolution, cacheId });
    cv_.notify_one();
    
    return { cacheId, entry->generation };
}

uint32_t WaveformRendererImpl::getWaveformData(WaveformHandle handle, 
                                               MinMaxPair* outPeakData, 
                                               float* outRMSData,
                                               uint32_t startFrame, 
                                               uint32_t numFrames) const {
    std::shared_lock lock(mutex_);
    auto it = cache_.find(handle.cacheId);
    if (it == cache_.end() || it->second->generation != handle.generation || !it->second->ready) {
        return 0;
    }
    
    const auto& peaks = it->second->peaks;
    const auto& rms = it->second->rms;
    
    if (startFrame >= peaks.size()) return 0;
    
    uint32_t available = static_cast<uint32_t>(peaks.size()) - startFrame;
    uint32_t toCopy = std::min(numFrames, available);
    
    if (outPeakData) {
        std::copy(peaks.begin() + startFrame, peaks.begin() + startFrame + toCopy, outPeakData);
    }
    if (outRMSData && !rms.empty()) {
        std::copy(rms.begin() + startFrame, rms.begin() + startFrame + toCopy, outRMSData);
    }
    
    return toCopy;
}

bool WaveformRendererImpl::isWaveformReady(WaveformHandle handle) const {
    std::shared_lock lock(mutex_);
    auto it = cache_.find(handle.cacheId);
    return (it != cache_.end() && it->second->generation == handle.generation && it->second->ready);
}

uint32_t WaveformRendererImpl::getWaveformDataSize(WaveformHandle handle) const {
    std::shared_lock lock(mutex_);
    auto it = cache_.find(handle.cacheId);
    if (it != cache_.end() && it->second->generation == handle.generation) {
        return static_cast<uint32_t>(it->second->peaks.size());
    }
    return 0;
}

bool WaveformRendererImpl::isHandleValid(WaveformHandle handle) const {
    std::shared_lock lock(mutex_);
    auto it = cache_.find(handle.cacheId);
    return (it != cache_.end() && it->second->generation == handle.generation && it->second->useCount > 0);
}

void WaveformRendererImpl::releaseWaveform(WaveformHandle handle) {
    std::unique_lock lock(mutex_);
    auto it = cache_.find(handle.cacheId);
    if (it != cache_.end() && it->second->generation == handle.generation) {
        if (it->second->useCount > 0) {
            it->second->useCount--;
        }
        it->second->lastAccessTicket = ++accessCounter_;
    }
}

void WaveformRendererImpl::invalidateCache(MediaID mediaId) {
    std::unique_lock lock(mutex_);
    // Remove all resolutions for this mediaId
    for (uint8_t r = 0; r < 5; ++r) {
        uint64_t key = (static_cast<uint64_t>(mediaId.toRaw()) << 8) | r;
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            auto entryIt = cache_.find(it->second);
            if (entryIt != cache_.end()) {
                entryIt->second->cancelled = true;
                cache_.erase(entryIt);
            }
            lookup_.erase(it);
        }
    }
}

void WaveformRendererImpl::update() {
    // Optional: could handle Main Thread notifications here if we had callbacks
}

void WaveformRendererImpl::workerLoop() {
    while (running_) {
        PendingJob job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this]() { return !running_ || !jobQueue_.empty(); });
            
            if (!running_) break;
            
            job = jobQueue_.front();
            jobQueue_.pop_front();
        }
        
        processJob(job);
    }
}

void WaveformRendererImpl::processJob(const PendingJob& job) {
    std::shared_ptr<CacheEntry> entry;
    {
        std::unique_lock lock(mutex_);
        auto it = cache_.find(job.cacheId);
        if (it == cache_.end()) return;
        entry = it->second;
    }

    if (entry->cancelled) return;

    AssetInfo info{};
    if (!registry_->getAssetInfo(job.mediaId, info)) {
        entry->ready = true;
        return;
    }

    // Resolve pathId to string using Layer 2 String Registry
    std::string filePath;
    bool hasPath = strings_->getString(info.pathId, filePath);
    
    auto reader = codecFactory_->createReader(hasPath ? filePath : "");
    if (!hasPath || !reader || !reader->isValid()) {
        // Fallback: flat peaks for missing files so the job still completes
        uint32_t ratio = getWaveformResolutionRatio(job.resolution);
        uint32_t peakCount = static_cast<uint32_t>((info.durationSamples + ratio - 1) / ratio);
        entry->peaks.assign(peakCount, { 0.0f, 0.0f });
        entry->rms.assign(peakCount, 0.0f);
        entry->ready = true;
        return;
    }

    uint32_t ratio = getWaveformResolutionRatio(job.resolution);
    uint64_t totalFrames = reader->getTotalFrames();
    uint32_t peakCount = static_cast<uint32_t>((totalFrames + ratio - 1) / ratio);
    
    entry->peaks.resize(peakCount);
    entry->rms.resize(peakCount);
    
    uint16_t numChannels = reader->getNumChannels();
    std::vector<float> buffer(1024 * numChannels);
    uint32_t framesRead = 0;
    uint32_t peakIdx = 0;
    
    float currentMin = 1.0f;
    float currentMax = -1.0f;
    double currentSumSquared = 0.0;
    uint32_t samplesInCurrentPeak = 0;

    while ((framesRead = reader->readFrames(buffer.data(), 1024)) > 0) {
        if (entry->cancelled) return;
        
        for (uint32_t f = 0; f < framesRead; ++f) {
            float frameMax = -1.0f;
            float frameMin = 1.0f;
            double frameSumSquared = 0.0;

            for (uint16_t c = 0; c < numChannels; ++c) {
                float s = buffer[f * numChannels + c];
                frameMin = std::min(frameMin, s);
                frameMax = std::max(frameMax, s);
                frameSumSquared += static_cast<double>(s) * static_cast<double>(s);
            }
            
            currentMin = std::min(currentMin, frameMin);
            currentMax = std::max(currentMax, frameMax);
            currentSumSquared += frameSumSquared / numChannels; // Average energy across channels
            
            samplesInCurrentPeak++;
            if (samplesInCurrentPeak >= ratio) {
                if (peakIdx < entry->peaks.size()) {
                    entry->peaks[peakIdx] = { currentMin, currentMax };
                    entry->rms[peakIdx] = static_cast<float>(std::sqrt(currentSumSquared / ratio));
                    peakIdx++;
                }
                currentMin = 1.0f;
                currentMax = -1.0f;
                currentSumSquared = 0.0;
                samplesInCurrentPeak = 0;
            }
        }
    }
    
    // Handle last partial peak
    if (samplesInCurrentPeak > 0 && peakIdx < entry->peaks.size()) {
        entry->peaks[peakIdx] = { currentMin, currentMax };
        entry->rms[peakIdx] = static_cast<float>(std::sqrt(currentSumSquared / samplesInCurrentPeak));
    }

    entry->ready = true;

    // Push to Registry (Phase 2)
    {
        AssetWaveformBlobs blobs;
        registry_->getWaveformBlobs(job.mediaId, blobs);
        blobs.peaks[job.resolution] = entry->peaks;
        blobs.rms[job.resolution] = entry->rms;
        registry_->setWaveformBlobs(job.mediaId, blobs);
    }
}

/**
 * @brief Concrete implementation of IWaveformSink.
 */
class WaveformSinkImpl : public IWaveformSink {
public:
    WaveformSinkImpl([[maybe_unused]] uint32_t sampleRate, uint16_t numChannels)
        : numChannels_(numChannels) {
        
        resolutions_ = {
            WaveformResolution::FULL,
            WaveformResolution::HIGH,
            WaveformResolution::MEDIUM,
            WaveformResolution::LOW,
            WaveformResolution::OVERVIEW
        };
        
        for (auto res : resolutions_) {
            ratios_[res] = getWaveformResolutionRatio(res);
            peaks_[res] = {};
            rms_[res] = {};
            currentMin_[res] = 1.0f;
            currentMax_[res] = -1.0f;
            currentSumSquared_[res] = 0.0;
            samplesInCurrentPeak_[res] = 0;
        }
    }

    void processFrames(const float* buffer, uint32_t numFrames, uint16_t channels) override {
        for (uint32_t f = 0; f < numFrames; ++f) {
            float frameMax = -1.0f;
            float frameMin = 1.0f;
            double frameSumSquared = 0.0;

            for (uint16_t c = 0; c < channels; ++c) {
                float s = buffer[f * channels + c];
                frameMin = std::min(frameMin, s);
                frameMax = std::max(frameMax, s);
                frameSumSquared += static_cast<double>(s) * static_cast<double>(s);
            }
            
            for (auto res : resolutions_) {
                currentMin_[res] = std::min(currentMin_[res], frameMin);
                currentMax_[res] = std::max(currentMax_[res], frameMax);
                currentSumSquared_[res] += frameSumSquared / channels;
                
                samplesInCurrentPeak_[res]++;
                if (samplesInCurrentPeak_[res] >= ratios_[res]) {
                    peaks_[res].push_back({ currentMin_[res], currentMax_[res] });
                    rms_[res].push_back(static_cast<float>(std::sqrt(currentSumSquared_[res] / ratios_[res])));
                    
                    currentMin_[res] = 1.0f;
                    currentMax_[res] = -1.0f;
                    currentSumSquared_[res] = 0.0;
                    samplesInCurrentPeak_[res] = 0;
                }
            }
        }
    }

    void finalize() override {
        for (auto res : resolutions_) {
            if (samplesInCurrentPeak_[res] > 0) {
                peaks_[res].push_back({ currentMin_[res], currentMax_[res] });
                rms_[res].push_back(static_cast<float>(std::sqrt(currentSumSquared_[res] / samplesInCurrentPeak_[res])));
            }
        }
    }

    void getPeaks(WaveformResolution res, std::vector<MinMaxPair>& outPeaks) const override {
        auto it = peaks_.find(res);
        if (it != peaks_.end()) {
            outPeaks = it->second;
        }
    }

    const std::unordered_map<WaveformResolution, std::vector<MinMaxPair>>& getAllPeaks() const override { return peaks_; }
    const std::unordered_map<WaveformResolution, std::vector<float>>& getAllRMS() const override { return rms_; }

private:

    [[maybe_unused]] uint16_t numChannels_;
    std::vector<WaveformResolution> resolutions_;
    std::unordered_map<WaveformResolution, uint32_t> ratios_;
    std::unordered_map<WaveformResolution, std::vector<MinMaxPair>> peaks_;
    std::unordered_map<WaveformResolution, std::vector<float>> rms_;
    
    std::unordered_map<WaveformResolution, float> currentMin_;
    std::unordered_map<WaveformResolution, float> currentMax_;
    std::unordered_map<WaveformResolution, double> currentSumSquared_;
    std::unordered_map<WaveformResolution, uint32_t> samplesInCurrentPeak_;
};

std::unique_ptr<IWaveformSink> WaveformRendererImpl::createSink(uint32_t sampleRate, uint16_t numChannels) {
    return std::make_unique<WaveformSinkImpl>(sampleRate, numChannels);
}

void WaveformRendererImpl::importWaveformData(MediaID mediaId, 
                                             const std::unordered_map<WaveformResolution, std::vector<MinMaxPair>>& peaks, 
                                             const std::unordered_map<WaveformResolution, std::vector<float>>& rms) {
    // 1. Update Registry (Phase 2)
    AssetWaveformBlobs blobs;
    blobs.peaks = peaks;
    blobs.rms = rms;
    registry_->setWaveformBlobs(mediaId, blobs);

    // 2. Update local cache (overwriting existing entries to prevent leaks/orphans)
    std::unique_lock lock(mutex_);
    for (const auto& [res, peakData] : peaks) {
        uint64_t key = (static_cast<uint64_t>(mediaId.toRaw()) << 8) | static_cast<uint8_t>(res);
        
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            auto entryIt = cache_.find(it->second);
            if (entryIt != cache_.end()) {
                // Overwrite in place to keep handles valid and preserve useCounts
                entryIt->second->peaks = peakData;
                auto rmsIt = rms.find(res);
                if (rmsIt != rms.end()) entryIt->second->rms = rmsIt->second;
                else entryIt->second->rms.clear();
                entryIt->second->ready = true;
                entryIt->second->lastAccessTicket = ++accessCounter_;
                continue;
            }
        }

        // Evict oldest unused entry if cache grows too large
        if (cache_.size() >= 100) {
            uint32_t oldestCacheId = 0;
            uint64_t oldestTicket = std::numeric_limits<uint64_t>::max();
            for (const auto& [id, cEntry] : cache_) {
                if (cEntry->useCount == 0 && cEntry->lastAccessTicket < oldestTicket) {
                    oldestTicket = cEntry->lastAccessTicket;
                    oldestCacheId = id;
                }
            }
            if (oldestCacheId != 0) {
                auto evictEntry = cache_[oldestCacheId];
                uint64_t evictKey = (static_cast<uint64_t>(evictEntry->mediaId.toRaw()) << 8) | static_cast<uint8_t>(evictEntry->resolution);
                lookup_.erase(evictKey);
                evictEntry->cancelled = true;
                cache_.erase(oldestCacheId);
            }
        }
        
        uint32_t cacheId = nextCacheId_++;
        auto entry = std::make_shared<CacheEntry>();
        entry->mediaId = mediaId;
        entry->resolution = res;
        entry->generation = 1;
        entry->peaks = peakData;
        entry->lastAccessTicket = ++accessCounter_;
        
        auto rmsIt = rms.find(res);
        if (rmsIt != rms.end()) entry->rms = rmsIt->second;
        
        entry->ready = true;
        cache_[cacheId] = entry;
        lookup_[key] = cacheId;
    }
}

uint32_t WaveformRendererImpl::getPeaksDirect(MediaID mediaId,
                                              uint64_t startSample,
                                              uint64_t endSample,
                                              float samplesPerPixel,
                                              MinMaxPair* outPeaks,
                                              uint32_t bufferSize) const {
    if (!outPeaks || bufferSize == 0 || endSample <= startSample) return 0;

    std::shared_lock lock(mutex_);

    // 1. Choose best resolution ensuring ratio <= samplesPerPixel to prevent staircasing
    WaveformResolution res;
    if (samplesPerPixel < 64.0f) res = WaveformResolution::FULL;
    else if (samplesPerPixel < 256.0f) res = WaveformResolution::HIGH;
    else if (samplesPerPixel < 1024.0f) res = WaveformResolution::MEDIUM;
    else if (samplesPerPixel < 4096.0f) res = WaveformResolution::LOW;
    else res = WaveformResolution::OVERVIEW;

    auto getEntry = [&](WaveformResolution r) -> std::shared_ptr<CacheEntry> {
        uint64_t key = (static_cast<uint64_t>(mediaId.toRaw()) << 8) | static_cast<uint8_t>(r);
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            auto entryIt = cache_.find(it->second);
            if (entryIt != cache_.end()) {
                return entryIt->second;
            }
        }
        return nullptr;
    };

    auto entry = getEntry(res);
    if (!entry || !entry->ready) {
        if (res != WaveformResolution::OVERVIEW) {
            res = WaveformResolution::OVERVIEW;
            entry = getEntry(res);
        }
    }

    if (!entry || !entry->ready) {
        return 0;
    }

    entry->lastAccessTicket = ++accessCounter_;

    uint32_t ratio = getWaveformResolutionRatio(res);
    double step = static_cast<double>(samplesPerPixel) / static_cast<double>(ratio);
    double startOffset = static_cast<double>(startSample) / static_cast<double>(ratio);

    for (uint32_t i = 0; i < bufferSize; ++i) {
        double pixelStart = startOffset + static_cast<double>(i) * step;
        double pixelEnd = startOffset + static_cast<double>(i + 1) * step;

        uint32_t idxStart = static_cast<uint32_t>(std::max(0.0, pixelStart));
        uint32_t idxEnd = static_cast<uint32_t>(std::max(0.0, std::ceil(pixelEnd)));

        if (idxStart >= entry->peaks.size()) {
            outPeaks[i] = { 0.0f, 0.0f };
            continue;
        }

        idxEnd = std::min(idxEnd, static_cast<uint32_t>(entry->peaks.size()));
        if (idxEnd <= idxStart) {
            idxEnd = idxStart + 1;
        }

        if (idxEnd == idxStart + 1) {
            outPeaks[i] = entry->peaks[idxStart];
        } else {
            float pMin = 1.0f;
            float pMax = -1.0f;
            for (uint32_t k = idxStart; k < idxEnd; ++k) {
                pMin = std::min(pMin, entry->peaks[k].min);
                pMax = std::max(pMax, entry->peaks[k].max);
            }
            if (pMin > pMax) {
                pMin = 0.0f;
                pMax = 0.0f;
            }
            outPeaks[i] = { pMin, pMax };
        }
    }
    return bufferSize;
}

bool WaveformRendererImpl::isResolutionReady(MediaID mediaId, WaveformResolution resolution) const {
    std::shared_lock lock(mutex_);
    uint64_t key = (static_cast<uint64_t>(mediaId.toRaw()) << 8) | static_cast<uint8_t>(resolution);
    auto it = lookup_.find(key);
    if (it != lookup_.end()) {
        auto entryIt = cache_.find(it->second);
        if (entryIt != cache_.end()) {
            return entryIt->second->ready;
        }
    }
    
    // Also check Registry (Phase 2)
    AssetWaveformBlobs blobs;
    if (registry_->getWaveformBlobs(mediaId, blobs)) {
        return blobs.peaks.find(resolution) != blobs.peaks.end();
    }
    
    return false;
}

std::unique_ptr<IWaveformRenderer> IWaveformRenderer::create(IMediaRegistry* registry, 
                                                             Layer2::IStringRegistry* strings,
                                                             ICodecFactory* codecFactory) {
    return std::make_unique<WaveformRendererImpl>(registry, strings, codecFactory);
}

} // namespace MediaManagement
