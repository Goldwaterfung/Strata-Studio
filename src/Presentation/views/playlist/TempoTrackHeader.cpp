// src/Presentation/views/playlist/TempoTrackHeader.cpp
#include "TempoTrackHeader.h"
#include "../theme.h"
#include <QPainter>
#include <QMouseEvent>
#include <cmath>

namespace presentation::views {

TempoTrackHeader::TempoTrackHeader(bridge::ITimelineController* timeline, QWidget* parent)
    : QWidget(parent)
    , m_timeline(timeline)
{
    setObjectName(QStringLiteral("TempoTrackHeader"));
    setFixedWidth(280);
    setMouseTracking(true);
}

void TempoTrackHeader::setCollapsed(bool collapsed)
{
    if (m_collapsed != collapsed) {
        m_collapsed = collapsed;
        update();
    }
}

void TempoTrackHeader::updateBpmReadout(double bpm)
{
    if (std::abs(m_currentBpm - bpm) > 0.05) {
        m_currentBpm = bpm;
        update();
    }
}

bool TempoTrackHeader::isInResizeZone(const QPointF& pos) const
{
    if (m_collapsed) return false;
    return pos.y() >= (height() - 5.0);
}

void TempoTrackHeader::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double w = width();
    const double h = height();

    // Base background
    p.fillRect(rect(), theme::Color::BgBase);

    if (m_collapsed) {
        // Collapsed styling: narrow strip with a tiny chevron
        p.setPen(theme::Color::TextMuted);
        p.setBrush(theme::Color::TextMuted);
        
        // Tiny downward triangle
        QPointF points[3] = {
            QPointF(w / 2.0 - 4.0, h / 2.0 - 2.0),
            QPointF(w / 2.0 + 4.0, h / 2.0 - 2.0),
            QPointF(w / 2.0, h / 2.0 + 2.0)
        };
        p.drawPolygon(points, 3);
        m_chevronRect = QRectF(w / 2.0 - 10.0, 0.0, 20.0, h);
    } else {
        // Expanded styling
        // Border lines
        p.setPen(QPen(QColor(0x24, 0x28, 0x31, 255), 1.0));
        p.drawLine(QPointF(0.0, h - 0.5), QPointF(w, h - 0.5));
        p.drawLine(QPointF(w - 0.5, 0.0), QPointF(w - 0.5, h));

        // Chevron location: top-right corner
        m_chevronRect = QRectF(w - 24.0, 4.0, 20.0, 20.0);
        
        // Upward chevron
        p.setPen(QPen(theme::Color::TextMuted, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(std::vector<QPointF>{
            QPointF(m_chevronRect.left() + 5.0, m_chevronRect.top() + 12.0),
            QPointF(m_chevronRect.left() + 10.0, m_chevronRect.top() + 7.0),
            QPointF(m_chevronRect.left() + 15.0, m_chevronRect.top() + 12.0)
        }.data(), 3);

        // "TEMPO" text
        p.setPen(theme::Color::TextPrimary);
        p.setFont(theme::Font::monospace(8, QFont::Bold));
        p.drawText(QRectF(8.0, 6.0, 100.0, 16.0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("TEMPO"));

        // Current BPM digital readout (only if there's enough space)
        if (h >= 40.0) {
            p.setPen(theme::Color::AccentGlow);
            p.setFont(theme::Font::monospace(10, QFont::Bold));
            QString bpmStr = QStringLiteral("%1 BPM").arg(m_currentBpm, 0, 'f', 1);
            p.drawText(QRectF(8.0, 26.0, 150.0, 20.0), Qt::AlignLeft | Qt::AlignVCenter, bpmStr);
        }
    }
}

void TempoTrackHeader::mousePressEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    
    // Check collapse chevron click
    if (m_chevronRect.contains(pos)) {
        m_collapsed = !m_collapsed;
        emit collapseToggled(m_collapsed);
        update();
        event->accept();
        return;
    }

    // Check height resize zone click
    if (isInResizeZone(pos)) {
        m_isResizing = true;
        m_dragAnchor = pos;
        m_dragStartHeight = height();
        setCursor(Qt::SizeVerCursor);
        event->accept();
        return;
    }

    event->ignore();
}

void TempoTrackHeader::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();

    if (m_isResizing) {
        const double deltaY = pos.y() - m_dragAnchor.y();
        const int newHeight = std::max(40, std::min(300, static_cast<int>(m_dragStartHeight + deltaY)));
        emit heightResizeRequested(newHeight);
        event->accept();
        return;
    }

    // Update cursor hover feedback
    if (isInResizeZone(pos)) {
        setCursor(Qt::SizeVerCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

    event->accept();
}

void TempoTrackHeader::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isResizing) {
        m_isResizing = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    event->ignore();
}

} // namespace presentation::views
