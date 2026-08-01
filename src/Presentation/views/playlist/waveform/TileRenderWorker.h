// src/Presentation/views/playlist/waveform/TileRenderWorker.h
#pragma once

#include <QObject>
#include <QImage>
#include <QPixmap>
#include <thread>
#include <atomic>
#include <functional>
#include "common/system_primitives.h"
#include "Core infrastructure/bridges/spsc_queue.h"
#include "Middle Bridge/telemetry/iwaveform_cache_provider.h"

namespace presentation::views {

struct TileKey {
    uint64_t mediaId;
    uint32_t zoomTier;
    uint32_t tileX;
    uint32_t colorARGB;
    uint32_t generation;

    bool operator==(const TileKey& o) const {
        return mediaId == o.mediaId &&
               zoomTier == o.zoomTier &&
               tileX == o.tileX &&
               colorARGB == o.colorARGB &&
               generation == o.generation;
    }
};

struct TileRequest {
    TileKey key;
    uint64_t fileStartFrame;
    uint64_t fileEndFrame;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
};

class TileRenderWorker : public QObject {
    Q_OBJECT

public:
    explicit TileRenderWorker(bridge::IWaveformCacheProvider* waveformCache, QObject* parent = nullptr);
    ~TileRenderWorker() override;

    void start();
    void stop();

    bool enqueue(const TileRequest& req);

    const QPixmap& getShimmerPixmap();

signals:
    void tileRendered(presentation::views::TileKey key, QImage image);

private:
    void renderLoop();
    void initShimmer();
    void renderTile(const TileRequest& req);

    bridge::IWaveformCacheProvider* waveformCache_{nullptr};
    Layer2::SPSCQueue<TileRequest, 128> requestQueue_;
    std::atomic<bool> running_{false};
    std::thread renderThread_;
    
    QImage shimmer_;
    QPixmap shimmerPixmap_;
    bool shimmerPixmapInitialized_{false};
};

} // namespace presentation::views

namespace std {
template <>
struct hash<presentation::views::TileKey> {
    std::size_t operator()(const presentation::views::TileKey& key) const noexcept {
        std::size_t h1 = std::hash<uint64_t>{}(key.mediaId);
        std::size_t h2 = std::hash<uint32_t>{}(key.zoomTier);
        std::size_t h3 = std::hash<uint32_t>{}(key.tileX);
        std::size_t h4 = std::hash<uint32_t>{}(key.colorARGB);
        std::size_t h5 = std::hash<uint32_t>{}(key.generation);

        std::size_t seed = h1;
        seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h4 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h5 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
} // namespace std
