// src/Presentation/views/playlist/TempoTrackCanvas.cpp
#include "TempoTrackCanvas.h"
#include "../theme.h"
#include <QPainter>
#include <QMouseEvent>
#include <cmath>
#include <algorithm>

namespace presentation::views {

TempoTrackCanvas::TempoTrackCanvas(bridge::ITimelineController* timeline,
                                   bridge::IInputModeController* inputMode,
                                   bridge::IArrangementController* arrangement,
                                   QWidget* parent)
    : QWidget(parent)
    , m_timeline(timeline)
    , m_inputMode(inputMode)
    , m_arrangement(arrangement)
{
    setObjectName(QStringLiteral("TempoTrackCanvas"));
    setMouseTracking(true);
}

void TempoTrackCanvas::setViewState(uint64_t startFrame, uint64_t endFrame, double zoomFactor)
{
    if (m_startFrame != startFrame || m_endFrame != endFrame || m_zoomFactor != zoomFactor) {
        m_startFrame = startFrame;
        m_endFrame = endFrame;
        m_zoomFactor = zoomFactor;
        refreshTimelineCache();
    }
}

void TempoTrackCanvas::refreshTimelineCache()
{
    if (!m_timeline) return;

    m_tempoPoints.clear();
    static constexpr uint32_t MAX_PTS = 256;
    bridge::VisualTempoPoint pts[MAX_PTS];
    uint32_t count = m_timeline->getTempoPoints(0, UINT64_MAX, pts, MAX_PTS);

    for (uint32_t i = 0; i < count; ++i) {
        m_tempoPoints.push_back({pts[i].framePosition, pts[i].bpm});
    }

    // Sort to be absolutely sure
    std::sort(m_tempoPoints.begin(), m_tempoPoints.end(), [](const LocalTempoPoint& a, const LocalTempoPoint& b) {
        return a.frame < b.frame;
    });

    update();
}

uint64_t TempoTrackCanvas::xToFrame(double x) const
{
    if (m_zoomFactor <= 0.0) return m_startFrame;
    const double frameD = static_cast<double>(m_startFrame) + (x / m_zoomFactor);
    return (frameD >= 0.0) ? static_cast<uint64_t>(frameD) : 0;
}

double TempoTrackCanvas::frameToX(uint64_t frame) const
{
    if (m_zoomFactor <= 0.0) return 0.0;
    const int64_t relFrame = static_cast<int64_t>(frame) - static_cast<int64_t>(m_startFrame);
    return static_cast<double>(relFrame) * m_zoomFactor;
}

double TempoTrackCanvas::bpmToY(double bpm) const
{
    const double h = height();
    if (h <= 0.0) return 0.0;
    const double clampedBpm = std::clamp(bpm, 20.0, 300.0);
    return h - ((clampedBpm - 20.0) / (300.0 - 20.0)) * h;
}

double TempoTrackCanvas::yToBpm(double y) const
{
    const double h = height();
    if (h <= 0.0) return 120.0;
    const double clampedY = std::clamp(y, 0.0, h);
    return 20.0 + ((h - clampedY) / h) * (300.0 - 20.0);
}

uint64_t TempoTrackCanvas::snapFrame(uint64_t frame) const
{
    if (!m_inputMode || !m_timeline) return frame;
    const bridge::SnapMode mode = m_inputMode->getSnapMode();
    if (mode == bridge::SnapMode::Free) return frame;

    if (mode == bridge::SnapMode::Bar) {
        uint32_t bar, beat, tick;
        m_timeline->frameToBBT(frame, bar, beat, tick);
        return m_timeline->bbtToFrame(bar, 1, 0);
    }

    uint32_t intervalTicks = 960;
    const uint32_t tpb = m_timeline->getTicksPerBeat();
    switch (mode) {
        case bridge::SnapMode::Note_1_2:           intervalTicks = tpb * 2; break;
        case bridge::SnapMode::Note_1_4:           intervalTicks = tpb;     break;
        case bridge::SnapMode::Note_1_8:           intervalTicks = tpb / 2; break;
        case bridge::SnapMode::Note_1_16:          intervalTicks = tpb / 4; break;
        case bridge::SnapMode::Note_1_32:          intervalTicks = tpb / 8; break;
        case bridge::SnapMode::Note_1_64:          intervalTicks = tpb / 16; break;
        case bridge::SnapMode::Note_1_2_Triplet:   intervalTicks = (tpb * 4) / 3; break;
        case bridge::SnapMode::Note_1_4_Triplet:   intervalTicks = (tpb * 2) / 3; break;
        case bridge::SnapMode::Note_1_8_Triplet:   intervalTicks = tpb / 3; break;
        case bridge::SnapMode::Note_1_16_Triplet:  intervalTicks = tpb / 6; break;
        case bridge::SnapMode::Note_1_32_Triplet:  intervalTicks = tpb / 12; break;
        case bridge::SnapMode::Note_1_64_Triplet:  intervalTicks = tpb / 24; break;
        default: break;
    }

    if (intervalTicks == 0) return frame;

    const uint64_t ticks = m_timeline->samplesToTicks(frame);
    const uint64_t low = (ticks / intervalTicks) * intervalTicks;
    const uint64_t high = low + intervalTicks;
    const uint64_t snappedTicks = (ticks - low < high - ticks) ? low : high;

    return m_timeline->ticksToSamples(snappedTicks);
}

int TempoTrackCanvas::findNodeUnderMouse(const QPointF& pos) const
{
    if (m_collapsed) return -1;
    for (size_t i = 0; i < m_tempoPoints.size(); ++i) {
        const double x = frameToX(m_tempoPoints[i].frame);
        const double y = bpmToY(m_tempoPoints[i].bpm);
        // Distance check (radius 6px)
        const double dx = pos.x() - x;
        const double dy = pos.y() - y;
        if (dx * dx + dy * dy <= 36.0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool TempoTrackCanvas::isInResizeZone(const QPointF& pos) const
{
    if (m_collapsed) return false;
    return pos.y() >= (height() - 5.0);
}

void TempoTrackCanvas::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double w = width();
    const double h = height();

    // Background
    p.fillRect(rect(), theme::Color::BgControl);

    // Collapsed visual indicator line
    m_collapsed = (h <= 15.0);

    // Grid (only in expanded view)
    if (!m_collapsed) {
        drawGrid(p);
    }

    if (m_tempoPoints.empty()) {
        // Draw flat line at default BPM
        const double currentBpm = m_timeline ? m_timeline->getBPM() : 120.0;
        const double y = bpmToY(currentBpm);
        p.setPen(QPen(theme::Color::AccentGlow, m_collapsed ? 1.5 : 2.0));
        p.drawLine(QPointF(0.0, y), QPointF(w, y));
    } else {
        // Neon Cyber-Mint step envelope drawing
        p.setPen(QPen(theme::Color::AccentGlow, m_collapsed ? 1.5 : 2.0));
        p.setBrush(Qt::NoBrush);

        double lastX = 0.0;
        double lastY = bpmToY(m_tempoPoints[0].bpm);

        // If the first point is not at 0, draw leading flat line
        const double firstX = frameToX(m_tempoPoints[0].frame);
        if (firstX > 0.0) {
            p.drawLine(QPointF(0.0, lastY), QPointF(firstX, lastY));
            lastX = firstX;
        }

        // Draw step lines connecting nodes
        for (const auto& pt : m_tempoPoints) {
            const double nextX = frameToX(pt.frame);
            const double nextY = bpmToY(pt.bpm);

            // Horizontal step segment
            p.drawLine(QPointF(lastX, lastY), QPointF(nextX, lastY));
            // Vertical transition segment
            p.drawLine(QPointF(nextX, lastY), QPointF(nextX, nextY));

            lastX = nextX;
            lastY = nextY;
        }

        // Draw trailing flat line from final node to right edge
        p.drawLine(QPointF(lastX, lastY), QPointF(w, lastY));

        // Draw nodes (circular handles) when expanded
        if (!m_collapsed) {
            p.setPen(QPen(theme::Color::BgControl, 1.0));
            p.setBrush(theme::Color::AccentGlow);
            for (const auto& pt : m_tempoPoints) {
                const double x = frameToX(pt.frame);
                const double y = bpmToY(pt.bpm);
                if (x >= 0.0 && x <= w) {
                    p.drawEllipse(QPointF(x, y), 4.5, 4.5);
                }
            }
        }
    }

    // Bottom border separator line
    p.setPen(QPen(QColor(0x24, 0x28, 0x31, 255), 1.0));
    p.drawLine(QPointF(0.0, h - 0.5), QPointF(w, h - 0.5));
}

void TempoTrackCanvas::drawGrid(QPainter& p)
{
    const double w = width();
    const double h = height();

    // 1. Draw horizontal BPM helper lines
    p.setFont(theme::Font::monospace(7, QFont::Normal));
    const double bpms[] = { 60.0, 120.0, 180.0, 240.0 };
    for (double b : bpms) {
        const double y = bpmToY(b);
        p.setPen(QPen(QColor(0x2d, 0x31, 0x3f, 100), 1.0, Qt::DashLine));
        p.drawLine(QPointF(0.0, y), QPointF(w, y));
        
        // Draw BPM values at the left edge
        p.setPen(theme::Color::TextMuted);
        p.drawText(QRectF(8.0, y - 10.0, 60.0, 10.0), Qt::AlignLeft | Qt::AlignBottom, QString::number(b, 'f', 0));
    }

    // 2. Draw vertical bar markers (synced scroll grid)
    if (!m_timeline || m_endFrame <= m_startFrame || m_zoomFactor <= 0.0) return;

    const double sampleRate     = m_timeline->getSampleRate();
    const double bpm            = m_timeline->getBPM();
    const double framesPerBeat  = (sampleRate * 60.0) / std::max(1.0, bpm);
    const double framesPerBar   = framesPerBeat * 4.0;

    uint64_t barStep = 1;
    while (framesPerBar * static_cast<double>(barStep) * m_zoomFactor < 40.0) {
        barStep *= 2;
    }

    const double visibleStartD = static_cast<double>(m_startFrame);
    const double visibleEndD   = static_cast<double>(m_endFrame);
    const uint64_t firstBarIdx = static_cast<uint64_t>(std::floor(visibleStartD / framesPerBar));

    p.setPen(QPen(QColor(0x24, 0x28, 0x31, 255), 1.0));
    for (uint64_t barIdx = firstBarIdx; ; barIdx += barStep) {
        const double barFrame = static_cast<double>(barIdx) * framesPerBar;
        if (barFrame > visibleEndD) break;

        if (barFrame >= visibleStartD) {
            const double x = frameToX(static_cast<uint64_t>(barFrame));
            p.drawLine(QPointF(x, 0.0), QPointF(x, h));
        }
    }
}

void TempoTrackCanvas::mousePressEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    m_dragAnchor = pos;

    if (isInResizeZone(pos)) {
        m_interaction = InteractionMode::ResizingHeight;
        m_dragStartHeight = height();
        setCursor(Qt::SizeVerCursor);
        event->accept();
        return;
    }

    const int clickedIdx = findNodeUnderMouse(pos);
    if (clickedIdx >= 0) {
        m_interaction = InteractionMode::DraggingNode;
        m_activeNodeIdx = clickedIdx;
        m_dragStartFrame = m_tempoPoints[static_cast<size_t>(clickedIdx)].frame;
        m_dragStartBpm = m_tempoPoints[static_cast<size_t>(clickedIdx)].bpm;
        event->accept();
        return;
    }

    event->ignore();
}

void TempoTrackCanvas::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();

    if (m_interaction == InteractionMode::ResizingHeight) {
        const double deltaY = pos.y() - m_dragAnchor.y();
        const int newHeight = std::max(40, std::min(300, static_cast<int>(m_dragStartHeight + deltaY)));
        emit heightResizeRequested(newHeight);
        event->accept();
        return;
    }

    if (m_interaction == InteractionMode::DraggingNode && m_activeNodeIdx >= 0) {
        // Calculate new value based on drag positions
        double newBpm = std::clamp(yToBpm(pos.y()), 20.0, 300.0);
        newBpm = std::round(newBpm * 10.0) / 10.0; // Round to 0.1 BPM
        
        uint64_t newFrame = m_dragStartFrame;
        if (m_activeNodeIdx > 0) {
            // Only non-zero nodes can drag horizontally
            newFrame = snapFrame(xToFrame(pos.x()));
            
            // Constrain dragging to adjacent nodes
            uint64_t minFrame = m_tempoPoints[static_cast<size_t>(m_activeNodeIdx - 1)].frame + 9600; // at least 1 beat spacer (~0.2s at 48k)
            uint64_t maxFrame = UINT64_MAX;
            if (m_activeNodeIdx + 1 < static_cast<int>(m_tempoPoints.size())) {
                maxFrame = m_tempoPoints[static_cast<size_t>(m_activeNodeIdx + 1)].frame - 9600;
            }
            newFrame = std::clamp(newFrame, minFrame, maxFrame);
        }

        // Apply update in model
        if (newFrame != m_dragStartFrame) {
            m_timeline->removeTempoPoint(m_dragStartFrame);
            m_timeline->addTempoPoint(newFrame, newBpm);
            m_dragStartFrame = newFrame;
        } else {
            m_timeline->addTempoPoint(newFrame, newBpm);
        }

        refreshTimelineCache();
        event->accept();
        return;
    }

    // Cursor updates
    if (isInResizeZone(pos)) {
        setCursor(Qt::SizeVerCursor);
    } else if (findNodeUnderMouse(pos) >= 0) {
        setCursor(Qt::PointingHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

    event->accept();
}

void TempoTrackCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_interaction != InteractionMode::None) {
        m_interaction = InteractionMode::None;
        m_activeNodeIdx = -1;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    event->ignore();
}

void TempoTrackCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    doubleClickEvent(event);
}

void TempoTrackCanvas::doubleClickEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    const int doubleClickedIdx = findNodeUnderMouse(pos);

    if (doubleClickedIdx >= 0) {
        // Delete the node (cannot delete node at 0)
        uint64_t frame = m_tempoPoints[static_cast<size_t>(doubleClickedIdx)].frame;
        if (frame > 0) {
            m_timeline->removeTempoPoint(frame);
            refreshTimelineCache();
            event->accept();
            return;
        }
    } else {
        // Add a new node
        const uint64_t frame = snapFrame(xToFrame(pos.x()));
        double bpm = std::clamp(yToBpm(pos.y()), 20.0, 300.0);
        bpm = std::round(bpm * 10.0) / 10.0;

        // Ensure we aren't duplicate adding at position 0
        if (frame > 0) {
            m_timeline->addTempoPoint(frame, bpm);
            refreshTimelineCache();
            event->accept();
            return;
        }
    }
    event->ignore();
}

} // namespace presentation::views
