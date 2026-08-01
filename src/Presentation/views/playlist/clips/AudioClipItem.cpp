// src/Presentation/views/playlist/clips/AudioClipItem.cpp
#include "AudioClipItem.h"

#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QVarLengthArray>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>

#include "theme.h"
#include "playlist/waveform/TileRenderWorker.h"
#include "playlist/waveform/WaveformTileCache.h"

namespace presentation::views {

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────

void AudioClipItem::paint(
    QPainter& p,
    const QRectF& clipRect,
    const bridge::VisualRegion& region,
    bridge::IWaveformCacheProvider* waveform,
    uint64_t viewStartFrame,
    uint64_t viewEndFrame,
    double zoomFactor,
    WaveformTileCache* tileCache,
    TileRenderWorker* tileWorker,
    std::unordered_set<TileKey>& inFlightTiles)
{
    if (clipRect.width() < 2.0 || clipRect.height() < 2.0) {
        return;
    }

    p.save();
    p.setClipRect(clipRect);

    // 1. Background fill
    drawClipBackground(p, clipRect, region.colorARGB, region.isMuted);

    // 2. Waveform tiles (or shimmer if not yet loaded)
    if (waveform && tileCache && tileWorker) {
        // Inset 1 px vertically for the label strip at top
        const double labelH = 16.0;
        QRectF waveRect(
            clipRect.left(),
            clipRect.top() + labelH,
            clipRect.width(),
            clipRect.height() - labelH - 1.0
        );

        if (waveRect.height() > 4.0) {
            const double spp_screen = 1.0 / zoomFactor;
            const double ratio = region.timelineToSourceRatio;
            const double spp_file = spp_screen * ratio;

            // Quantize zoom factor
            uint32_t quantizedZoom = 64;
            if (spp_file < 64.0) {
                quantizedZoom = 1;
                while (quantizedZoom < spp_file && quantizedZoom < 64) {
                    quantizedZoom *= 2;
                }
            } else {
                quantizedZoom = ((static_cast<uint32_t>(spp_file) + 63) / 64) * 64;
            }

            const uint64_t clipStartFrame = region.startFrame;
            const uint64_t clipEndFrame = region.startFrame + region.durationFrames;
            const uint64_t visibleStart = std::max(clipStartFrame, viewStartFrame);
            const uint64_t visibleEnd = std::min(clipEndFrame, viewEndFrame);

            if (visibleEnd > visibleStart) {
                const uint64_t fileStart = static_cast<uint64_t>(
                    static_cast<double>((visibleStart - clipStartFrame) + region.fileOffsetFrames) * ratio);
                const uint64_t fileEnd = static_cast<uint64_t>(
                    static_cast<double>((visibleEnd - clipStartFrame) + region.fileOffsetFrames) * ratio);

                const int r = static_cast<int>((region.colorARGB >> 16) & 0xFF);
                const int g = static_cast<int>((region.colorARGB >>  8) & 0xFF);
                const int b = static_cast<int>( region.colorARGB        & 0xFF);
                const QColor waveColor(r, g, b, region.isMuted ? 75 : 220);
                const uint32_t waveColorARGB = waveColor.rgba();

                const uint64_t tileFrameCount = 256ULL * quantizedZoom;
                const uint32_t tileX_min = static_cast<uint32_t>(fileStart / tileFrameCount);
                const uint32_t tileX_max = static_cast<uint32_t>(fileEnd / tileFrameCount);


                for (uint32_t tileX = tileX_min; tileX <= tileX_max; ++tileX) {
                    const uint64_t tileStartF = tileX * tileFrameCount;
                    const uint64_t tileEndF = tileStartF + tileFrameCount;

                    const uint64_t intersectStart = std::max(tileStartF, fileStart);
                    const uint64_t intersectEnd = std::min(tileEndF, fileEnd);

                    if (intersectStart >= intersectEnd) {
                        continue;
                    }

                    const double timelineStartOffset = (static_cast<double>(intersectStart) / ratio) - static_cast<double>(region.fileOffsetFrames);
                    const double timelineEndOffset = (static_cast<double>(intersectEnd) / ratio) - static_cast<double>(region.fileOffsetFrames);

                    const double destXStart = clipRect.left() + timelineStartOffset * zoomFactor;
                    const double destXEnd = clipRect.left() + timelineEndOffset * zoomFactor;

                    const QRectF destRect(
                        destXStart,
                        waveRect.top(),
                        destXEnd - destXStart,
                        waveRect.height()
                    );

                    const double srcXStart = static_cast<double>(intersectStart - tileStartF) / static_cast<double>(quantizedZoom);
                    const double srcXEnd = static_cast<double>(intersectEnd - tileStartF) / static_cast<double>(quantizedZoom);

                    TileKey key{
                        .mediaId = region.mediaId,
                        .zoomTier = quantizedZoom,
                        .tileX = tileX,
                        .colorARGB = waveColorARGB,
                        .generation = 0
                    };

                    const QPixmap* pixmap = tileCache->lookup(key);
                    if (pixmap) {
                        const QRectF srcRect(
                            srcXStart,
                            0.0,
                            srcXEnd - srcXStart,
                            static_cast<double>(pixmap->height())
                        );
                        p.drawPixmap(destRect, *pixmap, srcRect);
                    } else {
                        const QRectF srcRect(
                            srcXStart,
                            0.0,
                            srcXEnd - srcXStart,
                            waveRect.height()
                        );
                        // 1. Draw shimmer as base while tile is loading
                        p.drawPixmap(destRect, tileWorker->getShimmerPixmap(), srcRect);

                        // 2. Request asynchronous rendering of correct tile
                        if (inFlightTiles.find(key) == inFlightTiles.end()) {
                            TileRequest req{
                                .key = key,
                                .fileStartFrame = tileStartF,
                                .fileEndFrame = tileEndF,
                                .pixelWidth = 256,
                                .pixelHeight = static_cast<uint32_t>(std::ceil(waveRect.height()))
                            };
                            if (tileWorker->enqueue(req)) {
                                inFlightTiles.insert(key);
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Fade overlays
    if (region.fadeInFrames > 0 || region.fadeOutFrames > 0) {
        // Convert fade frames to pixels using the clip's own pixel width and timelineToSourceRatio
        const double framesPerPx = (clipRect.width() > 0.0 && region.durationFrames > 0)
            ? static_cast<double>(region.durationFrames) / clipRect.width()
            : 1.0;
        const double ratio = (region.timelineToSourceRatio > 0.0) ? region.timelineToSourceRatio : 1.0;
        const double fadeInProjectFrames = static_cast<double>(region.fadeInFrames) / ratio;
        const double fadeOutProjectFrames = static_cast<double>(region.fadeOutFrames) / ratio;
        const auto fadeInPx  = static_cast<uint32_t>(std::round(fadeInProjectFrames / framesPerPx));
        const auto fadeOutPx = static_cast<uint32_t>(std::round(fadeOutProjectFrames / framesPerPx));
        drawFadeOverlay(p, clipRect, fadeInPx, fadeOutPx);
    }

    // 4. Gain badge (skip at unity ±0.01 dB to reduce visual noise)
    if (std::fabs(region.gainLinear - 1.0f) > 0.001f) {
        drawGainBadge(p, clipRect, region.gainLinear);
    }

    // 5. Clip label
    drawClipLabel(p, clipRect, region);

    // 6. Border / selection highlight
    drawClipBorder(p, clipRect, region.isSelected);

    p.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private paint helpers
// ─────────────────────────────────────────────────────────────────────────────

void AudioClipItem::drawClipBackground(
    QPainter& p,
    const QRectF& clipRect,
    uint32_t colorARGB,
    bool isMuted)
{
    // Extract track colour tint from colorARGB (ARGB 32-bit)
    const int r = static_cast<int>((colorARGB >> 16) & 0xFF);
    const int g = static_cast<int>((colorARGB >>  8) & 0xFF);
    const int b = static_cast<int>( colorARGB        & 0xFF);

    // Dark tinted base blended toward BgSurface on mute
    QColor base(r, g, b);
    base = base.darker(isMuted ? 300 : 220);
    base.setAlpha(isMuted ? 130 : 200);

    const QRectF bRect = clipRect.adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(base);
    p.drawRoundedRect(bRect, 3.0, 3.0);

    // Subtle vertical gradient to give depth
    QLinearGradient grad(clipRect.topLeft(), clipRect.bottomLeft());
    grad.setColorAt(0.0, QColor(255, 255, 255, 18));
    grad.setColorAt(1.0, QColor(0, 0, 0, 22));
    p.setBrush(grad);
    p.drawRoundedRect(bRect, 3.0, 3.0);

    // Distinct darker top bar (15px) for move handle
    if (clipRect.height() > 15.0) {
        QRectF topBarRect = clipRect;
        topBarRect.setHeight(15.0);
        QColor topBarColor = base.darker(150);
        topBarColor.setAlpha(180);
        p.setBrush(topBarColor);
        p.drawRoundedRect(topBarRect.adjusted(0.5, 0.5, -0.5, 0.0), 3.0, 3.0);
    }
}


void AudioClipItem::drawFadeOverlay(
    QPainter& p,
    const QRectF& clipRect,
    uint32_t fadeInPx,
    uint32_t fadeOutPx)
{
    // Fade-in: linear gradient from opaque-dark to transparent (left edge)
    if (fadeInPx > 0) {
        const double w = static_cast<double>(fadeInPx);
        QRectF fadeZone(clipRect.left(), clipRect.top(),
                        std::min(w, clipRect.width()), clipRect.height());
        QLinearGradient fadeIn(fadeZone.topLeft(), fadeZone.topRight());
        fadeIn.setColorAt(0.0, QColor(0, 0, 0, 160));
        fadeIn.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(fadeZone, fadeIn);

        // Handle indicator line
        QColor indicatorColor = theme::Color::AccentGlow;
        indicatorColor.setAlpha(180);
        p.setPen(QPen(indicatorColor, 1.0));
        p.drawLine(
            QPointF(fadeZone.right(), clipRect.top()),
            QPointF(fadeZone.right(), clipRect.bottom())
        );
    }

    // Fade-out: linear gradient from transparent to opaque-dark (right edge)
    if (fadeOutPx > 0) {
        const double w = static_cast<double>(fadeOutPx);
        QRectF fadeZone(clipRect.right() - std::min(w, clipRect.width()),
                        clipRect.top(),
                        std::min(w, clipRect.width()),
                        clipRect.height());
        QLinearGradient fadeOut(fadeZone.topLeft(), fadeZone.topRight());
        fadeOut.setColorAt(0.0, QColor(0, 0, 0, 0));
        fadeOut.setColorAt(1.0, QColor(0, 0, 0, 160));
        p.fillRect(fadeZone, fadeOut);

        // Handle indicator line
        QColor indicatorColor = theme::Color::AccentGlow;
        indicatorColor.setAlpha(180);
        p.setPen(QPen(indicatorColor, 1.0));
        p.drawLine(
            QPointF(fadeZone.left(), clipRect.top()),
            QPointF(fadeZone.left(), clipRect.bottom())
        );
    }
}

void AudioClipItem::drawGainBadge(
    QPainter& p,
    const QRectF& clipRect,
    float gainLinear)
{
    // Convert linear gain to dB for display
    const float gaindB = (gainLinear > 0.0001f)
        ? 20.0f * std::log10(gainLinear)
        : -60.0f;

    // Position badge in bottom-right corner of clip
    constexpr double badgeW = 36.0;
    constexpr double badgeH = 13.0;
    const QRectF badgeRect(
        clipRect.right() - badgeW - 3.0,
        clipRect.bottom() - badgeH - 3.0,
        badgeW, badgeH
    );

    // Background
    p.setPen(Qt::NoPen);
    QColor badgeBg = theme::Color::BgBase;
    badgeBg.setAlpha(200);
    p.setBrush(badgeBg);
    p.drawRoundedRect(badgeRect, 2.0, 2.0);

    // Text — dB value in monospaced font
    QFont mono = theme::Font::monospace(7, QFont::Medium);
    p.setFont(mono);
    const QColor gainColor = (gaindB > 3.0f)
        ? theme::Color::AccentRecord // clipping warning
        : theme::Color::AccentGlow;
    p.setPen(gainColor);

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%+.1fdB", static_cast<double>(gaindB));
    p.drawText(badgeRect, Qt::AlignCenter, QString::fromLatin1(buf));
}

void AudioClipItem::drawClipLabel(
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
        clipRect.top() + 1.0,
        clipRect.width() - 8.0 - badgeWidth,
        15.0
    );

    p.setFont(theme::Font::primary(7, QFont::Medium));
    p.setPen(region.isMuted ? theme::Color::TextMuted : theme::Color::TextPrimary);

    const QString text = QString::fromUtf8(region.name);
    const int maxCharApprox = static_cast<int>(labelRect.width() / 5.5);
    const QString elided = (text.length() <= maxCharApprox)
        ? text
        : p.fontMetrics().elidedText(text, Qt::ElideRight, static_cast<int>(labelRect.width()));
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

void AudioClipItem::drawClipBorder(
    QPainter& p,
    const QRectF& clipRect,
    bool isSelected)
{
    const QRectF borderRect = clipRect.adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(Qt::NoBrush);

    if (isSelected) {
        // Double-ring selection glow
        QColor outerRing = theme::Color::AccentGlow;
        outerRing.setAlpha(60);
        QColor innerRing = theme::Color::AccentGlow;
        innerRing.setAlpha(200);
        p.setPen(QPen(outerRing, 3.0));
        p.drawRoundedRect(borderRect, 3.0, 3.0);
        p.setPen(QPen(innerRing, 1.0));
        p.drawRoundedRect(borderRect, 3.0, 3.0);
    } else {
        // Subtle edge definition
        p.setPen(QPen(QColor(255, 255, 255, 25), 1.0));
        p.drawRoundedRect(borderRect, 3.0, 3.0);
    }
}

} // namespace presentation::views
