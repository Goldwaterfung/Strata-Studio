#include "media_registry_impl.h"
#include "imedia_storage.h"

namespace MediaManagement {

MediaRegistryImpl::MediaRegistryImpl() = default;

MediaID MediaRegistryImpl::registerAsset(const AssetInfo& info) {
    std::unique_lock lock(mutex_);
    
    MediaID mediaId = info.mediaId;
    if (!mediaId.isValid()) {
        uint32_t id = nextId_.fetch_add(1);
        uint32_t generation = 1; // Initial generation
        mediaId = MediaID{id, generation};
    } else {
        uint32_t currentMax = nextId_.load();
        if (mediaId.id >= currentMax) {
            nextId_.store(mediaId.id + 1);
        }
    }
    
    RegistryEntry entry;
    entry.info = info;
    entry.info.mediaId = mediaId; // Ensure the info matches the assigned ID
    entry.generation = mediaId.generation;
    
    assets_[mediaId.id] = entry;
    
    return mediaId;
}

bool MediaRegistryImpl::getAssetInfo(MediaID id, AssetInfo& outInfo) const {
    std::shared_lock lock(mutex_);
    
    auto it = assets_.find(id.id);
    if (it != assets_.end() && it->second.generation == id.generation) {
        outInfo = it->second.info;
        return true;
    }
    
    return false;
}

bool MediaRegistryImpl::updateAssetInfo(MediaID id, const AssetInfo& info) {
    std::unique_lock lock(mutex_);
    
    auto it = assets_.find(id.id);
    if (it != assets_.end() && it->second.generation == id.generation) {
        it->second.info = info;
        it->second.info.mediaId = id; // Safeguard ID integrity
        return true;
    }
    
    return false;
}

bool MediaRegistryImpl::removeAsset(MediaID id) {
    std::unique_lock lock(mutex_);
    
    auto it = assets_.find(id.id);
    if (it != assets_.end() && it->second.generation == id.generation) {
        // We could keep the ID and increment generation to "burn" it, 
        // but removing is fine if nextId_ is monotonic and doesn't wrap quickly.
        // For DAW safety, we increment generation if we were to reuse slots.
        // Here we just remove it.
        assets_.erase(it);
        return true;
    }
    
    return false;
}

uint32_t MediaRegistryImpl::getAssetCount() const {
    std::shared_lock lock(mutex_);
    return static_cast<uint32_t>(assets_.size());
}

bool MediaRegistryImpl::setAnalysisBlobs(MediaID id, const AssetAnalysisBlobs& blobs) {
    std::unique_lock lock(mutex_);
    auto it = assets_.find(id.id);
    if (it != assets_.end() && it->second.generation == id.generation) {
        it->second.analysisBlobs = blobs;
        if (storage_) storage_->saveAnalysisBlobs(id, blobs);
        return true;
    }
    return false;
}

bool MediaRegistryImpl::getAnalysisBlobs(MediaID id, AssetAnalysisBlobs& outBlobs) const {
    std::shared_lock lock(mutex_);
    auto it = assets_.find(id.id);
    if (it != assets_.end() && it->second.generation == id.generation) {
        // Lazy load from storage if memory is empty
        if (it->second.analysisBlobs.spectralFlux.empty() && storage_) {
            lock.unlock();
            std::unique_lock writeLock(mutex_);
            // Double check after lock upgrade
            auto it2 = assets_.find(id.id);
            if (it2 != assets_.end() && it2->second.generation == id.generation) {
                if (it2->second.analysisBlobs.spectralFlux.empty()) {
                    storage_->loadAnalysisBlobs(id, it2->second.analysisBlobs);
                }
                outBlobs = it2->second.analysisBlobs;
                return true;
            }
            return false;
        }
        outBlobs = it->second.analysisBlobs;
        return true;
    }
    return false;
}

bool MediaRegistryImpl::setWaveformBlobs(MediaID id, const AssetWaveformBlobs& blobs) {
    std::unique_lock lock(mutex_);
    auto it = assets_.find(id.id);
    if (it != assets_.end() && it->second.generation == id.generation) {
        it->second.waveformBlobs = blobs;
        if (storage_) storage_->saveWaveformBlobs(id, blobs);
        return true;
    }
    return false;
}

bool MediaRegistryImpl::getWaveformBlobs(MediaID id, AssetWaveformBlobs& outBlobs) const {
    std::shared_lock lock(mutex_);
    auto it = assets_.find(id.id);
    if (it != assets_.end() && it->second.generation == id.generation) {
        // Lazy load from storage if memory is empty
        if (it->second.waveformBlobs.peaks.empty() && storage_) {
            lock.unlock();
            std::unique_lock writeLock(mutex_);
            auto it2 = assets_.find(id.id);
            if (it2 != assets_.end() && it2->second.generation == id.generation) {
                if (it2->second.waveformBlobs.peaks.empty()) {
                    storage_->loadWaveformBlobs(id, it2->second.waveformBlobs);
                }
                outBlobs = it2->second.waveformBlobs;
                return true;
            }
            return false;
        }
        outBlobs = it->second.waveformBlobs;
        return true;
    }
    return false;
}

std::vector<MediaID> MediaRegistryImpl::getAllMediaIDs() const {
    std::shared_lock lock(mutex_);
    std::vector<MediaID> ids;
    ids.reserve(assets_.size());
    
    for (const auto& [id, entry] : assets_) {
        ids.push_back({id, entry.generation});
    }
    
    return ids;
}

bool MediaRegistryImpl::serialize(const char* filePath) const {
    std::shared_lock lock(mutex_);
    FILE* file = std::fopen(filePath, "wb");
    if (!file) return false;

    uint32_t magic = 0x4D444941; // 'MDIA'
    uint32_t version = 1;
    uint32_t nextId = nextId_.load();
    uint32_t count = static_cast<uint32_t>(assets_.size());

    std::fwrite(&magic, 4, 1, file);
    std::fwrite(&version, 4, 1, file);
    std::fwrite(&nextId, 4, 1, file);
    std::fwrite(&count, 4, 1, file);

    for (const auto& [id, entry] : assets_) {
        std::fwrite(&id, 4, 1, file);
        std::fwrite(&entry.generation, 4, 1, file);
        std::fwrite(&entry.info, sizeof(AssetInfo), 1, file);
        // Note: Blobs are saved via storage_ when set, not in the main project file.
    }

    std::fclose(file);
    return true;
}

bool MediaRegistryImpl::deserialize(const char* filePath) {
    std::unique_lock lock(mutex_);
    FILE* file = std::fopen(filePath, "rb");
    if (!file) return false;

    uint32_t magic = 0, version = 0, nextId = 0, count = 0;
    if (std::fread(&magic, 4, 1, file) != 1 || magic != 0x4D444941) {
        std::fclose(file);
        return false;
    }
    std::fread(&version, 4, 1, file);
    std::fread(&nextId, 4, 1, file);
    std::fread(&count, 4, 1, file);

    assets_.clear();
    nextId_.store(nextId);

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = 0, generation = 0;
        std::fread(&id, 4, 1, file);
        std::fread(&generation, 4, 1, file);
        AssetInfo info;
        std::fread(&info, sizeof(AssetInfo), 1, file);
        
        RegistryEntry entry;
        entry.info = info;
        entry.generation = generation;
        // Blobs will be lazy-loaded on first access via storage_
        assets_[id] = entry;
    }

    std::fclose(file);
    return true;
}

std::unique_ptr<IMediaRegistry> IMediaRegistry::create() {
    return std::make_unique<MediaRegistryImpl>();
}

} // namespace MediaManagement
