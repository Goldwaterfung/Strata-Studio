// src/Presentation/views/playlist/clips/PatternClipItem.h
#pragma once

#include <QPainter>
#include <QRectF>

#include "telemetry/ipattern_data_provider.h"
#include "timeline/iarrangement_controller.h"

namespace presentation::views {

/**
 * @brief Stateless, all-static paint helper for Pattern (MIDI) clip thumbnails.
 *
 * Renders a compact piano-roll preview: each MIDI note is drawn as a filled
 * rectangle. Pitch maps to Y (MIDI 0 = bottom, 127 = top). Start frame maps
 * to X. Duration maps to width. Velocity maps to alpha.
 *
 * Stack buffer: VisualNoteEvent notes[MAX_NOTES] — no heap allocation.
 */
class PatternClipItem {
public:
    PatternClipItem() = delete;

    /**
     * @brief Paint a single pattern clip into clipRect.
     *
     * @param p           Active QPainter.
     * @param clipRect    Bounding rectangle in widget coordinates.
     * @param region      Bridge VisualRegion snapshot (POD, read-only).
     * @param patternData Pattern data provider — stack-safe query.
     */
    static void paint(
        QPainter& p,
        const QRectF& clipRect,
        const bridge::VisualRegion& region,
        bridge::IPatternDataProvider* patternData,
        int noteColorMode = 0
    );

    static constexpr uint32_t MAX_NOTES = 256;

private:
    static void drawClipBackground(QPainter& p, const QRectF& clipRect,
                                   uint32_t colorARGB, bool isMuted);
    static void drawNoteEvents(QPainter& p, const QRectF& innerRect,
                               const bridge::VisualNoteEvent* notes,
                               uint32_t count,
                               uint64_t regionStart, uint64_t regionDuration,
                               int noteColorMode, uint32_t regionColorARGB,
                               bool isMuted);
    static void drawClipLabel(QPainter& p, const QRectF& clipRect,
                              const bridge::VisualRegion& region);
    static void drawClipBorder(QPainter& p, const QRectF& clipRect,
                               bool isSelected);
};

} // namespace presentation::views
