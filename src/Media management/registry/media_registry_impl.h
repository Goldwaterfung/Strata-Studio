#pragma once

#include "imedia_registry.h"
#include <unordered_map>
#include <shared_mutex>
#include <atomic>

namespace MediaManagement {

/**
 * @brief Thread-safe implementation of IMediaRegistry.
 * 
 * Uses a generation-counted handle system to ensure safe access to assets
 * even across background analysis threads.
 */
class MediaRegistryImpl : public IMediaRegistry {
public:
    MediaRegistryImpl();
    ~MediaRegistryImpl() override = default;

    // IMediaRegistry implementation
    MediaID registerAsset(const AssetInfo& info) override;
    bool getAssetInfo(MediaID id, AssetInfo& outInfo) const override;
    bool updateAssetInfo(MediaID id, const AssetInfo& info) override;
    bool removeAsset(MediaID id) override;
    uint32_t getAssetCount() const override;
    std::vector<MediaID> getAllMediaIDs() const override;
    
    bool setAnalysisBlobs(MediaID id, const AssetAnalysisBlobs& blobs) override;
    bool getAnalysisBlobs(MediaID id, AssetAnalysisBlobs& outBlobs) const override;

    bool setWaveformBlobs(MediaID id, const AssetWaveformBlobs& blobs) override;
    bool getWaveformBlobs(MediaID id, AssetWaveformBlobs& outBlobs) const override;
    
    bool serialize(const char* filePath) const override;
    bool deserialize(const char* filePath) override;

private:
    // Internal structure to track asset with generation
    struct RegistryEntry {
        AssetInfo info;
        uint32_t generation;
        mutable AssetAnalysisBlobs analysisBlobs;
        mutable AssetWaveformBlobs waveformBlobs;
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint32_t, RegistryEntry> assets_;
    
    std::atomic<uint32_t> nextId_{0};
    class IMediaStorage* storage_{nullptr};
};

} // namespace MediaManagement
