// src/Presentation/views/playlist/clips/AudioClipItem.h
#pragma once

#include <QPainter>
#include <QRectF>
#include <QPixmap>
#include <QGuiApplication>
#include <unordered_map>

#include "telemetry/iwaveform_cache_provider.h"
#include "timeline/iarrangement_controller.h"

#include "playlist/waveform/TileRenderWorker.h"

namespace presentation::views {

class WaveformTileCache;

/**
 * @brief Stateless, all-static paint helper for Audio clip thumbnails.
 *
 * No instances are ever constructed. All state is passed as arguments.
 * Zero heap allocation — every buffer is stack-allocated or caller-owned.
 *
 * The waveform fetch follows the non-blocking pattern:
 *   - If WaveformSegment::isLoaded == false, an animated shimmer is drawn
 *     and requestWaveformLoad() is called once (idempotent).
 *   - The actual decimation runs on the background waveform butler thread.
 */
class AudioClipItem {
public:
    AudioClipItem() = delete;

    /**
     * @brief Paint a single audio clip into clipRect.
     *
     * @param p             Active QPainter (caller manages state/save/restore).
     * @param clipRect      Bounding rectangle in widget coordinates.
     * @param region        Bridge VisualRegion snapshot (POD, read-only).
     * @param waveform      Waveform cache provider — may be called to fetch peaks
     *                      or request async load. Never blocks the GUI thread.
     * @param viewStartFrame The start frame of the current viewport.
     * @param viewEndFrame   The end frame of the current viewport.
     * @param zoomFactor     The horizontal zoom factor.
     * @param tileCache     The tile cache.
     * @param tileWorker    The tile render worker.
     * @param inFlightTiles  The set of tiles currently being rendered in background.
     */
    static void paint(
        QPainter& p,
        const QRectF& clipRect,
        const bridge::VisualRegion& region,
        bridge::IWaveformCacheProvider* waveform,
        uint64_t viewStartFrame,
        uint64_t viewEndFrame,
        double zoomFactor,
        WaveformTileCache* tileCache,
        TileRenderWorker* tileWorker,
        std::unordered_set<TileKey>& inFlightTiles
    );

private:
    static void drawClipBackground(QPainter& p, const QRectF& clipRect,
                                   uint32_t colorARGB, bool isMuted);
    static void drawFadeOverlay(QPainter& p, const QRectF& clipRect,
                                uint32_t fadeInPx, uint32_t fadeOutPx);
    static void drawGainBadge(QPainter& p, const QRectF& clipRect,
                              float gainLinear);
    static void drawClipLabel(QPainter& p, const QRectF& clipRect,
                              const bridge::VisualRegion& region);
    static void drawClipBorder(QPainter& p, const QRectF& clipRect,
                               bool isSelected);
};

} // namespace presentation::views

