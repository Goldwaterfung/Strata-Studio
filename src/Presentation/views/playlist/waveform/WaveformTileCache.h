// src/Presentation/views/playlist/waveform/WaveformTileCache.h
#pragma once

#include <unordered_map>
#include <list>
#include <QPixmap>
#include "TileRenderWorker.h"

namespace presentation::views {

class WaveformTileCache {
public:
    static constexpr size_t MAX_TILES = 500;

    struct Entry {
        QPixmap pixmap;
        std::list<TileKey>::iterator lruIt;
    };

    WaveformTileCache() = default;
    ~WaveformTileCache() = default;

    const QPixmap* lookup(const TileKey& key);
    void insert(const TileKey& key, QPixmap pixmap);
    void invalidateMedia(MediaID id);
    void clear();
    
    void setProvider(bridge::IWaveformCacheProvider* provider) { m_provider = provider; }
    
    template <typename Func>
    void forEachTile(Func&& func) const {
        for (const auto& [key, entry] : tiles_) {
            func(key, entry.pixmap);
        }
    }

private:
    void evictOne();

    bridge::IWaveformCacheProvider* m_provider{nullptr};
    std::list<TileKey> lruList_;
    std::unordered_map<TileKey, Entry> tiles_;
};

} // namespace presentation::views
