// src/Presentation/views/playlist/clips/PatternClipItem.cpp
#include "PatternClipItem.h"

#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>
#include <cstring>

#include "theme.h"

namespace presentation::views {

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────

void PatternClipItem::paint(
    QPainter& p,
    const QRectF& clipRect,
    const bridge::VisualRegion& region,
    bridge::IPatternDataProvider* patternData,
    int noteColorMode)
{
    if (clipRect.width() < 2.0 || clipRect.height() < 2.0) {
        return;
    }

    p.save();
    p.setClipRect(clipRect);

    // 1. Background
    drawClipBackground(p, clipRect, region.colorARGB, region.isMuted);

    // 2. MIDI note thumbnails
    if (patternData && region.durationFrames > 0) {
        // Stack-allocated — no heap allocation (plan §2-C)
        bridge::VisualNoteEvent notes[MAX_NOTES];
        const uint32_t count = patternData->getNoteEventsForRegion(
            region.id, notes, MAX_NOTES);

        if (count > 0) {
            const double labelH = 14.0;
            const QRectF innerRect(
                clipRect.left() + 1.0,
                clipRect.top()  + labelH,
                clipRect.width()  - 2.0,
                clipRect.height() - labelH - 1.0
            );

            if (innerRect.height() > 4.0) {
                drawNoteEvents(p, innerRect, notes, count,
                               region.startFrame, region.durationFrames,
                               noteColorMode, region.colorARGB, region.isMuted);
            }
        }
    }

    // 3. Label and border
    drawClipLabel(p, clipRect, region);
    drawClipBorder(p, clipRect, region.isSelected);

    p.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

void PatternClipItem::drawClipBackground(
    QPainter& p,
    const QRectF& clipRect,
    uint32_t colorARGB,
    bool isMuted)
{
    // Derive a deep purple-tinted background from colorARGB, blended toward
    // AccentMIDI (#A855F7) to maintain MIDI semantic colour identity
    const int r = static_cast<int>((colorARGB >> 16) & 0xFF);
    const int g = static_cast<int>((colorARGB >>  8) & 0xFF);
    const int b = static_cast<int>( colorARGB        & 0xFF);

    QColor base(r, g, b);
    base = base.darker(isMuted ? 320 : 130);
    base.setAlpha(isMuted ? 120 : 255);

    QPainterPath path;
    path.addRoundedRect(clipRect.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
    p.fillPath(path, base);

    // Subtle header strip (AccentMIDI tint)
    QRectF header(clipRect.left(), clipRect.top(), clipRect.width(), 14.0);
    QLinearGradient headerGrad(header.topLeft(), header.bottomLeft());
    headerGrad.setColorAt(0.0, QColor(0xA8, 0x55, 0xF7, isMuted ? 60 : 100));
    headerGrad.setColorAt(1.0, QColor(0xA8, 0x55, 0xF7, 0));
    p.fillRect(header, headerGrad);
}

void PatternClipItem::drawNoteEvents(
    QPainter& p,
    const QRectF& innerRect,
    const bridge::VisualNoteEvent* notes,
    uint32_t count,
    uint64_t regionStart,
    uint64_t regionDuration,
    int noteColorMode,
    uint32_t regionColorARGB,
    bool isMuted)
{
    if (regionDuration == 0 || count == 0) {
        return;
    }

    // Find pitch range to auto-scale the Y axis
    uint8_t pitchMin = 127;
    uint8_t pitchMax = 0;
    uint32_t pitchSum = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (notes[i].pitch < pitchMin) pitchMin = notes[i].pitch;
        if (notes[i].pitch > pitchMax) pitchMax = notes[i].pitch;
        pitchSum += notes[i].pitch;
    }

    uint8_t pitchRange = (pitchMax > pitchMin) ? (pitchMax - pitchMin) : 0;
    if (pitchRange < 12) {
        // Center a 12-semitone (1 octave) window around the average pitch
        const uint8_t midPitch = static_cast<uint8_t>(pitchSum / count);
        pitchMin = (midPitch >= 6) ? (midPitch - 6) : 0;
        pitchMax = (pitchMin + 12 <= 127) ? (pitchMin + 12) : 127;
        pitchRange = pitchMax - pitchMin;
    }

    const double scaleX = innerRect.width()  / static_cast<double>(regionDuration);
    const double scaleY = innerRect.height() / static_cast<double>(pitchRange + 1);

    // Minimum note height / width for visibility
    const double noteH = std::max(1.0, scaleY - 0.5);

    p.setPen(Qt::NoPen);

    // Static 16-color channel palette
    static const QColor channelColors[16] = {
        QColor(239, 68, 68),   // Red
        QColor(249, 115, 22),  // Orange
        QColor(245, 158, 11),  // Amber
        QColor(234, 179, 8),   // Yellow
        QColor(132, 204, 22),  // Lime
        QColor(34, 197, 94),   // Green
        QColor(16, 185, 129),  // Emerald
        QColor(20, 184, 166),  // Teal
        QColor(6, 182, 212),   // Cyan
        QColor(14, 165, 233),  // Sky
        QColor(59, 130, 246),  // Blue
        QColor(99, 102, 241),  // Indigo
        QColor(139, 92, 246),  // Violet
        QColor(168, 85, 247),  // Purple
        QColor(217, 70, 239),  // Fuchsia
        QColor(236, 72, 153)   // Pink
    };

    for (uint32_t i = 0; i < count; ++i) {
        const auto& note = notes[i];

        double x = 0.0;
        double w = 0.0;

        // Draw note if it falls inside the region's duration window
        if (note.startFrame < regionStart) {
            // Check if note overlaps the start edge of the clip
            const uint64_t noteEnd = note.startFrame + note.durationFrames;
            if (noteEnd <= regionStart) {
                continue;
            }
            const uint64_t overlappingDuration = noteEnd - regionStart;
            x = innerRect.left();
            w = std::max(1.5, static_cast<double>(overlappingDuration) * scaleX);
        } else {
            const uint64_t relStart = note.startFrame - regionStart;
            if (relStart >= regionDuration) {
                continue;
            }
            x = innerRect.left() + static_cast<double>(relStart) * scaleX;
            w = std::max(1.5, static_cast<double>(note.durationFrames) * scaleX);
        }

        // Y: higher pitch → higher on screen (lower Y value)
        // Ensure pitch is clamped within our active rendering range to prevent drawing out of bounds
        const uint8_t clampedPitch = std::clamp(note.pitch, pitchMin, pitchMax);
        const double pitchRel = static_cast<double>(clampedPitch - pitchMin);
        const double y = innerRect.bottom() - (pitchRel + 1.0) * scaleY;

        // Velocity → alpha (MIDI 127 = fully opaque)
        int alpha = 100 + static_cast<int>(
            static_cast<double>(note.velocity) / 127.0 * 155.0);

        if (isMuted) {
            alpha /= 3;
        }

        QColor noteColor;
        if (noteColorMode == 0) {
            // NTE (Note Pitch): Map pitch (0-127) to a color wheel hue (0.0 to 360.0)
            noteColor.setHsv(static_cast<int>((note.pitch / 127.0) * 360.0), 220, 255, alpha);
        } else if (noteColorMode == 1) {
            // CHN (MIDI Channel): Index into standard 16-color array
            noteColor = channelColors[note.pitch % 16];
            noteColor.setAlpha(alpha);
        } else {
            // PAT (Pattern Color): Use region base color
            const int r = static_cast<int>((regionColorARGB >> 16) & 0xFF);
            const int g = static_cast<int>((regionColorARGB >>  8) & 0xFF);
            const int b = static_cast<int>( regionColorARGB        & 0xFF);
            noteColor = QColor(r, g, b, alpha);
        }

        p.setBrush(noteColor);
        p.drawRect(QRectF(x, y, std::min(w, innerRect.right() - x), noteH));
    }
}

void PatternClipItem::drawClipLabel(
    QPainter& p,
    const QRectF& clipRect,
    const bridge::VisualRegion& region)
{
    if (clipRect.width() < 24.0) {
        return;
    }

    double badgeWidth = (region.hasCustomComment && clipRect.width() > 40.0) ? 14.0 : 0.0;

    const QRectF labelRect(
        clipRect.left() + 4.0,
        clipRect.top()  + 1.0,
        clipRect.width() - 8.0 - badgeWidth,
        13.0
    );

    p.setFont(theme::Font::primary(7, QFont::Medium));
    p.setPen(region.isMuted ? theme::Color::TextMuted : theme::Color::TextPrimary);

    const QString text = QString::fromUtf8(region.name);
    const QString elided = p.fontMetrics().elidedText(
        text, Qt::ElideRight, static_cast<int>(labelRect.width()));
    p.drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft, elided);

    // Draw a small dialogue badge indicator next to the clip title when there's a comment
    if (region.hasCustomComment && clipRect.width() > 40.0) {
        QRectF badgeRect(clipRect.right() - 14.0, clipRect.top() + 3.0, 10.0, 8.0);
        p.setPen(theme::Color::TextMuted);
        QColor badgeColor(region.colorARGB);
        p.setBrush(badgeColor.darker(120));
        p.drawRoundedRect(badgeRect, 1.0, 1.0);
    }
}

void PatternClipItem::drawClipBorder(
    QPainter& p,
    const QRectF& clipRect,
    bool isSelected)
{
    QPainterPath border;
    border.addRoundedRect(clipRect.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
    p.setBrush(Qt::NoBrush);

    if (isSelected) {
        p.setPen(QPen(QColor(0xA8, 0x55, 0xF7, 60), 3.0));
        p.drawPath(border);
        p.setPen(QPen(QColor(0xA8, 0x55, 0xF7, 220), 1.0));
        p.drawPath(border);
    } else {
        p.setPen(QPen(QColor(255, 255, 255, 20), 1.0));
        p.drawPath(border);
    }
}

} // namespace presentation::views
