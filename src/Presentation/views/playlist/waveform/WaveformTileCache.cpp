// src/Presentation/views/playlist/waveform/WaveformTileCache.cpp
#include "WaveformTileCache.h"
#include <unordered_set>

namespace presentation::views {

const QPixmap* WaveformTileCache::lookup(const TileKey& key) {
    auto it = tiles_.find(key);
    if (it == tiles_.end()) {
        return nullptr;
    }
    lruList_.splice(lruList_.begin(), lruList_, it->second.lruIt);
    return &it->second.pixmap;
}

void WaveformTileCache::insert(const TileKey& key, QPixmap pixmap) {
    auto it = tiles_.find(key);
    if (it != tiles_.end()) {
        it->second.pixmap = std::move(pixmap);
        lruList_.splice(lruList_.begin(), lruList_, it->second.lruIt);
        return;
    }

    if (tiles_.size() >= MAX_TILES) {
        evictOne();
    }
    lruList_.push_front(key);
    tiles_[key] = Entry{std::move(pixmap), lruList_.begin()};
}

void WaveformTileCache::invalidateMedia(MediaID id) {
    const uint64_t rawId = id.toRaw();
    for (auto it = tiles_.begin(); it != tiles_.end(); ) {
        if (it->first.mediaId == rawId) {
            lruList_.erase(it->second.lruIt);
            it = tiles_.erase(it);
        } else {
            ++it;
        }
    }
    if (m_provider && id.isValid()) {
        m_provider->releaseWaveform(id);
    }
}

void WaveformTileCache::clear() {
    if (m_provider) {
        std::unordered_set<uint64_t> uniqueMediaIds;
        for (const auto& [key, _] : tiles_) {
            uniqueMediaIds.insert(key.mediaId);
        }
        for (uint64_t rawId : uniqueMediaIds) {
            m_provider->releaseWaveform(MediaID::fromRaw(rawId));
        }
    }
    tiles_.clear();
    lruList_.clear();
}

void WaveformTileCache::evictOne() {
    if (lruList_.empty()) {
        return;
    }
    TileKey oldestKey = lruList_.back();
    lruList_.pop_back();
    tiles_.erase(oldestKey);
}

} // namespace presentation::views
