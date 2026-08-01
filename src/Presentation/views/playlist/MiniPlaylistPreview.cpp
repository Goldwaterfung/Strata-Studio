// src/Presentation/views/playlist/MiniPlaylistPreview.cpp
#include "MiniPlaylistPreview.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include "theme.h"

namespace presentation::views {

MiniPlaylistPreview::MiniPlaylistPreview(
    bridge::IArrangementController* arrangement,
    bridge::ITimelineController*    timeline,
    QWidget* parent)
    : QWidget(parent)
    , m_arrangement(arrangement)
    , m_timeline(timeline)
{
    setObjectName(QStringLiteral("MiniPlaylistPreview"));
    setFixedHeight(24); // Compact bar height
    setCursor(Qt::PointingHandCursor);
}

void MiniPlaylistPreview::setViewport(uint64_t startFrame, uint64_t endFrame)
{
    if (m_viewStart != startFrame || m_viewEnd != endFrame) {
        m_viewStart = startFrame;
        m_viewEnd   = endFrame;
        update();
    }
}

void MiniPlaylistPreview::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double w = static_cast<double>(width());
    const double h = static_cast<double>(height());

    // 1. Dark Background
    p.fillRect(rect(), theme::Color::BgBase);

    // Subtle horizontal center grid line
    QColor centerLineColor = theme::Color::BgControl;
    centerLineColor.setAlpha(100);
    p.setPen(QPen(centerLineColor, 1.0));
    p.drawLine(QPointF(0.0, h / 2.0), QPointF(w, h / 2.0));

    // 2. Query and Draw All Regions (Stack Allocation, Zero Dynamic Heap Allocations!)
    if (m_arrangement && m_totalFrames > 0) {
        bridge::VisualRegion regions[PREVIEW_MAX_REGIONS];
        const uint32_t count = m_arrangement->getRegionsInViewport(
            0, m_totalFrames, regions, PREVIEW_MAX_REGIONS);

        for (uint32_t i = 0; i < count; ++i) {
            const double rx = (static_cast<double>(regions[i].startFrame) / m_totalFrames) * w;
            const double rw = (static_cast<double>(regions[i].durationFrames) / m_totalFrames) * w;
            
            // Map the trackId dynamically to a vertical index row (8 lanes total, 2px height per block)
            const int row = static_cast<int>(regions[i].trackId.toRaw() % 6);
            const double ry = 2.0 + static_cast<double>(row) * 3.2;

            if (rw >= 0.5) {
                // Color derived from original region palette with subtle transparency for background blend
                QColor col = QColor::fromRgba(regions[i].colorARGB);
                col.setAlpha(120);

                p.fillRect(QRectF(rx, ry, std::max(1.0, rw), 2.2), col);
            }
        }
    }

    // 3. Draw Visible Viewport Bracket Overlay
    if (m_totalFrames > 0 && m_viewEnd > m_viewStart) {
        const double bx = (static_cast<double>(m_viewStart) / m_totalFrames) * w;
        const double bw = (static_cast<double>(m_viewEnd - m_viewStart) / m_totalFrames) * w;

        // Semi-transparent glowing bracket body
        QRectF bracketRect(bx, 0.0, std::max(3.0, bw), h);
        QColor glowBg = theme::Color::AccentGlow;
        glowBg.setAlpha(18);
        p.fillRect(bracketRect, glowBg); // Glow base

        // Side border highlighting
        QColor sidePen = theme::Color::AccentGlow;
        sidePen.setAlpha(120);
        p.setPen(QPen(sidePen, 1.0));
        p.drawLine(QPointF(bx, 0.0), QPointF(bx, h));
        p.drawLine(QPointF(bx + bw, 0.0), QPointF(bx + bw, h));

        // Thin horizontal highlight lines
        QColor topBottomPen = theme::Color::AccentGlow;
        topBottomPen.setAlpha(50);
        p.setPen(QPen(topBottomPen, 1.0));
        p.drawLine(QPointF(bx, 0.5), QPointF(bx + bw, 0.5));
        p.drawLine(QPointF(bx, h - 0.5), QPointF(bx + bw, h - 0.5));
    }
}

void MiniPlaylistPreview::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        handleSeek(event->position().x());
    }
}

void MiniPlaylistPreview::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        handleSeek(event->position().x());
    }
}

void MiniPlaylistPreview::handleSeek(double mouseX)
{
    if (width() <= 0 || m_totalFrames == 0) return;

    double ratio = mouseX / static_cast<double>(width());
    ratio = std::clamp(ratio, 0.0, 1.0);

    const uint64_t targetFrame = static_cast<uint64_t>(ratio * static_cast<double>(m_totalFrames));
    
    // We want the seek position to center the viewport bracket if possible,
    // otherwise scroll it directly. Let's broadcast the seeked start frame.
    const uint64_t viewportSize = m_viewEnd > m_viewStart ? (m_viewEnd - m_viewStart) : 480000;
    
    uint64_t newStart = 0;
    if (targetFrame > (viewportSize / 2)) {
        newStart = targetFrame - (viewportSize / 2);
    }
    
    // Clamp to make sure we don't scroll past the right edge
    if (newStart + viewportSize > m_totalFrames) {
        newStart = (m_totalFrames > viewportSize) ? (m_totalFrames - viewportSize) : 0;
    }

    emit viewportSeekRequested(newStart);
}

} // namespace presentation::views
