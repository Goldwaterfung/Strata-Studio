// src/Presentation/views/playlist/PlaylistTimelineRuler.cpp
#include "PlaylistTimelineRuler.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>
#include <QMenu>
#include "dialogs/DAWInputDialog.h"
#include <QColorDialog>
#include <QContextMenuEvent>

#include "../theme.h"

namespace presentation::views {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

PlaylistTimelineRuler::PlaylistTimelineRuler(bridge::ITimelineController* timeline,
                                             bridge::IInputModeController* inputMode,
                                             bridge::IArrangementController* arrangement,
                                             QWidget* parent)
    : QWidget(parent)
    , m_timeline(timeline)
    , m_inputMode(inputMode)
    , m_arrangement(arrangement)
{
    setObjectName(QStringLiteral("PlaylistTimelineRuler"));
    setMouseTracking(true);
    setFixedHeight(40);

    // Initial timeline cache refresh
    refreshTimelineCache();
}

// ─────────────────────────────────────────────────────────────────────────────
// Setters
// ─────────────────────────────────────────────────────────────────────────────

void PlaylistTimelineRuler::setViewState(uint64_t startFrame, uint64_t endFrame, double zoomFactor)
{
    if (startFrame != m_startFrame || endFrame != m_endFrame || zoomFactor != m_zoomFactor) {
        m_startFrame = startFrame;
        m_endFrame   = endFrame;
        m_zoomFactor = zoomFactor;

        // Update cached playhead pixel coordinate
        m_cachedPlayheadX = frameToX(m_playheadFrame);
        update();
    }
}

void PlaylistTimelineRuler::setPlayheadFrame(uint64_t frame)
{
    if (frame == m_playheadFrame) {
        return;
    }
    m_playheadFrame = frame;

    const double newX = frameToX(frame);
    // Repaint only if pixel position shifted by at least 1.0 px
    if (std::abs(newX - m_cachedPlayheadX) >= 1.0) {
        m_cachedPlayheadX = newX;
        update();
    }
}

void PlaylistTimelineRuler::setLoopState(bool enabled, uint64_t start, uint64_t end)
{
    if (m_loopEnabled == enabled && m_loopStart == start && m_loopEnd == end) {
        return;
    }
    m_loopEnabled = enabled;
    m_loopStart = start;
    m_loopEnd = end;
    update();
}

void PlaylistTimelineRuler::refreshTimelineCache()
{
    if (!m_timeline) {
        return;
    }

    m_loopEnabled = m_timeline->isLooping();
    m_loopStart   = m_timeline->getLoopStart();
    m_loopEnd     = m_timeline->getLoopEnd();

    // Query named markers across the entire timeline
    m_markerCount = m_timeline->getMarkersInRange(0, UINT64_MAX, m_markers, MAX_MARKERS);
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
// Painting (Reads cached members only - zero allocation, no bridge calls)
// ─────────────────────────────────────────────────────────────────────────────

void PlaylistTimelineRuler::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 1. Draw base background
    p.fillRect(rect(), theme::Color::BgBase);

    // 2. Draw bar/beat divisions grid ticks & labels
    drawBarGrid(p);

    // 3. Draw loop region highlight (if active)
    drawLoopRegion(p);

    // 4. Draw named project markers
    drawNamedMarkers(p);

    // 5. Draw bottom separator line
    p.setPen(QPen(QColor(0x24, 0x28, 0x31, 255), 1.0));
    const double h = static_cast<double>(height());
    const double w = static_cast<double>(width());
    p.drawLine(QPointF(0.0, h - 0.5), QPointF(w, h - 0.5));

    // 6. Draw playhead on top
    drawPlayhead(p);
}

void PlaylistTimelineRuler::drawBarGrid(QPainter& p)
{
    if (m_endFrame <= m_startFrame || m_zoomFactor <= 0.0 || !m_timeline) {
        return;
    }

    uint32_t startBar, startBeat, startTick;
    m_timeline->frameToBBT(m_startFrame, startBar, startBeat, startTick);

    // Estimate frames per bar at start to determine barStep density
    uint64_t currentBarFrame = m_timeline->bbtToFrame(startBar, 1, 0);
    uint64_t nextBarFrame = m_timeline->bbtToFrame(startBar + 1, 1, 0);
    double estFramesPerBar = static_cast<double>(nextBarFrame > currentBarFrame ? nextBarFrame - currentBarFrame : 48000.0 * 2.0);

    uint32_t barStep = 1;
    while (estFramesPerBar * static_cast<double>(barStep) * m_zoomFactor < 40.0) {
        barStep *= 2;
    }

    p.setFont(theme::Font::monospace(8, QFont::Bold));

    for (uint32_t bar = startBar; ; bar += barStep) {
        uint64_t barFrame = m_timeline->bbtToFrame(bar, 1, 0);
        if (barFrame > m_endFrame) {
            break;
        }

        if (barFrame >= m_startFrame) {
            const double x = frameToX(barFrame);

            // Major bar tick
            p.setPen(QPen(theme::Color::TextPrimary, 1.0));
            p.drawLine(QPointF(x, 26.0), QPointF(x, 40.0));

            // Bar label
            p.setPen(theme::Color::TextPrimary);
            p.drawText(QRectF(x + 3.0, 14.0, 100.0, 12.0), Qt::AlignLeft | Qt::AlignVCenter, QString::number(bar));
        }

        // Level of Detail grid rendering inside each bar
        if (barStep == 1) {
            uint8_t num, den;
            m_timeline->getTimeSignatureAtFrame(barFrame, num, den);
            
            uint64_t barStartTick = m_timeline->samplesToTicks(barFrame);
            uint64_t barEndFrame = m_timeline->bbtToFrame(bar + 1, 1, 0);
            uint64_t nextBarStartTick = m_timeline->samplesToTicks(barEndFrame);
            uint64_t barTicks = nextBarStartTick - barStartTick;

            if (num > 0) {
                uint64_t ticksPerBeat = barTicks / num;
                double barWidthPx = static_cast<double>(barEndFrame - barFrame) * m_zoomFactor;
                double beatWidthPx = barWidthPx / static_cast<double>(num);

                // Determine subdivision level of detail
                uint64_t subdivTicks = ticksPerBeat;
                bool drawSubdivs = false;

                if (beatWidthPx >= 450.0 && ticksPerBeat >= 16) {
                    subdivTicks = ticksPerBeat / 16;
                    drawSubdivs = true;
                } else if (beatWidthPx >= 200.0 && ticksPerBeat >= 8) {
                    subdivTicks = ticksPerBeat / 8;
                    drawSubdivs = true;
                } else if (beatWidthPx >= 80.0 && ticksPerBeat >= 4) {
                    subdivTicks = ticksPerBeat / 4;
                    drawSubdivs = true;
                } else if (beatWidthPx >= 30.0 && ticksPerBeat >= 2) {
                    subdivTicks = ticksPerBeat / 2;
                    drawSubdivs = true;
                } else if (beatWidthPx >= 12.0) {
                    subdivTicks = ticksPerBeat;
                    drawSubdivs = true;
                }

                if (drawSubdivs && subdivTicks > 0) {
                    for (uint64_t t = subdivTicks; t < barTicks; t += subdivTicks) {
                        uint64_t T = barStartTick + t;
                        uint64_t subdivFrame = m_timeline->ticksToSamples(T);
                        if (subdivFrame > m_endFrame) {
                            break;
                        }
                        if (subdivFrame < m_startFrame) {
                            continue;
                        }

                        double x = frameToX(subdivFrame);

                        // Determine the highest priority grid category this subdivision matches
                        if (t % ticksPerBeat == 0) {
                            // Quarter note (Beat)
                            uint32_t beatNum = static_cast<uint32_t>((t / ticksPerBeat) + 1);

                            // Beat Tick
                            p.setPen(QPen(theme::Color::TextMuted, 1.0));
                            p.drawLine(QPointF(x, 30.0), QPointF(x, 40.0));

                            // Beat Label
                            if (beatWidthPx >= 100.0) {
                                p.setPen(theme::Color::TextMuted);
                                p.setFont(theme::Font::monospace(7, QFont::Normal));
                                p.drawText(QRectF(x + 3.0, 16.0, 100.0, 10.0), Qt::AlignLeft | Qt::AlignVCenter,
                                           QString("%1.%2").arg(bar).arg(beatNum));
                            }
                        }
                        else if (t % (ticksPerBeat / 2) == 0) {
                            // Eighth note
                            p.setPen(QPen(QColor(157, 178, 191, 120), 1.0)); // Faded TextMuted (#9DB2BF)
                            p.drawLine(QPointF(x, 33.0), QPointF(x, 40.0));
                        }
                        else if (t % (ticksPerBeat / 4) == 0) {
                            // Sixteenth note
                            p.setPen(QPen(QColor(157, 178, 191, 60), 1.0)); // Fainter TextMuted
                            p.drawLine(QPointF(x, 35.0), QPointF(x, 40.0));

                            // Sixteenth Label
                            if (beatWidthPx >= 450.0) {
                                uint32_t beatNum = static_cast<uint32_t>((t / ticksPerBeat) + 1);
                                uint32_t stepNum = static_cast<uint32_t>(((t % ticksPerBeat) / (ticksPerBeat / 4)) + 1);
                                p.setPen(QColor(157, 178, 191, 100)); // Faint text
                                p.setFont(theme::Font::monospace(6, QFont::Normal));
                                p.drawText(QRectF(x + 2.0, 17.0, 100.0, 8.0), Qt::AlignLeft | Qt::AlignVCenter,
                                           QString("%1.%2.%3").arg(bar).arg(beatNum).arg(stepNum));
                            }
                        }
                        else if (t % (ticksPerBeat / 8) == 0) {
                            // Thirty-second note
                            p.setPen(QPen(QColor(157, 178, 191, 30), 1.0)); // Extremely faded
                            p.drawLine(QPointF(x, 37.0), QPointF(x, 40.0));
                        }
                        else if (t % (ticksPerBeat / 16) == 0) {
                            // Sixty-fourth note
                            p.setPen(QPen(QColor(157, 178, 191, 15), 1.0)); // Barely visible tick
                            p.drawLine(QPointF(x, 38.5), QPointF(x, 40.0));
                        }
                    }
                }
            }
        }
    }
}

void PlaylistTimelineRuler::drawLoopRegion(QPainter& p)
{
    if (!m_loopEnabled || m_loopEnd <= m_loopStart || m_endFrame <= m_startFrame) {
        return;
    }

    const double xStart = frameToX(m_loopStart);
    const double xEnd   = frameToX(m_loopEnd);

    if (xEnd <= 0.0 || xStart >= static_cast<double>(width())) {
        return;
    }

    // Clip draw rectangle to widget bounds
    const double left  = std::max(0.0, xStart);
    const double right = std::min(static_cast<double>(width()), xEnd);

    if (right <= left) {
        return;
    }

    // Glassmorphic neon-mint glow for loop bar (y = 16.0 to 22.0)
    QColor loopColor = theme::Color::AccentGlow;
    loopColor.setAlphaF(static_cast<float>(0.15));
    p.fillRect(QRectF(left, 16.0, right - left, 6.0), loopColor);

    // Accent line at the top of the loop bar
    p.setPen(QPen(theme::Color::AccentGlow, 1.5));
    p.drawLine(QPointF(left, 17.0), QPointF(right, 17.0));

    // Draw handles (small triangles or ticks at start and end)
    p.setPen(QPen(theme::Color::AccentGlow, 1.0));
    p.setBrush(theme::Color::AccentGlow);

    if (xStart >= 0.0 && xStart <= static_cast<double>(width())) {
        // Start handle (pointing right)
        QPointF points[3] = {
            QPointF(xStart, 16.0),
            QPointF(xStart + 5.0, 19.0),
            QPointF(xStart, 22.0)
        };
        p.drawPolygon(points, 3);
    }

    if (xEnd >= 0.0 && xEnd <= static_cast<double>(width())) {
        // End handle (pointing left)
        QPointF points[3] = {
            QPointF(xEnd, 16.0),
            QPointF(xEnd - 5.0, 19.0),
            QPointF(xEnd, 22.0)
        };
        p.drawPolygon(points, 3);
    }
}

void PlaylistTimelineRuler::drawNamedMarkers(QPainter& p)
{
    p.setFont(theme::Font::monospace(7, QFont::Normal));

    for (uint32_t i = 0; i < m_markerCount; ++i) {
        const double x = frameToX(m_markers[i].framePosition);
        if (x < 0.0 || x > static_cast<double>(width())) {
            continue;
        }

        // Parse custom color or fall back to neon Cyber-Mint
        const QColor markerColor = (m_markers[i].colorARGB != 0)
            ? QColor(m_markers[i].colorARGB)
            : theme::Color::AccentGlow;

        // Calculate text size dynamically for the comment box
        QString text = QString::fromUtf8(m_markers[i].label);
        QFontMetrics fm(p.font());
        double textWidth = static_cast<double>(fm.horizontalAdvance(text));
        double textHeight = static_cast<double>(fm.height());

        double boxPaddingX = 6.0;
        double boxPaddingY = 2.0;
        double boxWidth = textWidth + (boxPaddingX * 2.0);
        double boxHeight = textHeight + (boxPaddingY * 2.0);
        double boxY = 2.0;
        double boxX = x - (boxWidth / 2.0);

        // Clamp to fit within the ruler widget bounds horizontally
        boxX = std::max(0.0, std::min(boxX, static_cast<double>(width()) - boxWidth));

        QRectF boxRect(boxX, boxY, boxWidth, boxHeight);

        // 1. Draw marker vertical line starting from the bottom of the comment box
        p.setPen(QPen(markerColor, 1.0));
        p.drawLine(QPointF(x, boxY + boxHeight), QPointF(x, static_cast<double>(height())));

        // 2. Draw solid rounded rectangle comment box (higher z-depth, on top of the line)
        p.setBrush(markerColor);
        p.setPen(QPen(markerColor.darker(150), 1.0)); // subtle dark border for definition
        p.drawRoundedRect(boxRect, 3.0, 3.0);

        // 3. Determine contrast text color based on background luminance
        double luminance = (0.299 * markerColor.red() + 0.587 * markerColor.green() + 0.114 * markerColor.blue()) / 255.0;
        QColor textColor = (luminance > 0.5) ? QColor(0, 0, 0) : QColor(255, 255, 255);

        // 4. Draw the text inside the comment box
        p.setBrush(Qt::NoBrush);
        p.setPen(textColor);
        p.drawText(boxRect, Qt::AlignCenter, text);
    }
}

void PlaylistTimelineRuler::drawPlayhead(QPainter& p)
{
    if (m_cachedPlayheadX < 0.0 || m_cachedPlayheadX > static_cast<double>(width())) {
        return;
    }

    // Draw bright neon red playhead handle (inverted house/triangle)
    p.setPen(QPen(theme::Color::AccentRecord, 1.0));
    p.setBrush(theme::Color::AccentRecord);

    const double h = static_cast<double>(height());

    // Inverted pentagon/triangle playhead indicator starting at y = 16.0
    QPointF points[5] = {
        QPointF(m_cachedPlayheadX - 6.0, 16.0),
        QPointF(m_cachedPlayheadX + 6.0, 16.0),
        QPointF(m_cachedPlayheadX + 6.0, 22.0),
        QPointF(m_cachedPlayheadX,       28.0),
        QPointF(m_cachedPlayheadX - 6.0, 22.0)
    };
    p.drawPolygon(points, 5);

    // Fine playhead trace line down the height of the ruler starting at y = 28.0
    p.setPen(QPen(theme::Color::AccentRecord, 1.0));
    p.drawLine(QPointF(m_cachedPlayheadX, 28.0), QPointF(m_cachedPlayheadX, h));
}

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate Mapping Helpers
// ─────────────────────────────────────────────────────────────────────────────

uint64_t PlaylistTimelineRuler::xToFrame(double x) const
{
    if (m_zoomFactor <= 0.0) {
        return m_startFrame;
    }
    const double frameD = static_cast<double>(m_startFrame) + (x / m_zoomFactor);
    return (frameD >= 0.0) ? static_cast<uint64_t>(frameD) : 0;
}

double PlaylistTimelineRuler::frameToX(uint64_t frame) const
{
    if (m_zoomFactor <= 0.0) {
        return 0.0;
    }
    const int64_t relFrame = static_cast<int64_t>(frame) - static_cast<int64_t>(m_startFrame);
    return static_cast<double>(relFrame) * m_zoomFactor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse Event Handlers & Interaction State Machine
// ─────────────────────────────────────────────────────────────────────────────

void PlaylistTimelineRuler::mousePressEvent(QMouseEvent* event)
{
    const QPointF pos = event->position(); // Qt6 position API
    m_dragAnchorPos   = pos;

    // Check if clicking near a marker first
    for (uint32_t i = 0; i < m_markerCount; ++i) {
        double x = frameToX(m_markers[i].framePosition);
        if (std::abs(pos.x() - x) <= 8.0) {
            if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier)) {
                m_interaction = InteractionMode::DraggingMarker;
                m_draggedMarkerUuid = m_markers[i].uuid;
                event->accept();
                return;
            }
        }
    }

    // 1. Detect loop boundaries interaction if loop is active
    if (m_loopEnabled && m_loopEnd > m_loopStart) {
        const double xStart = frameToX(m_loopStart);
        const double xEnd   = frameToX(m_loopEnd);

        // Check Loop Start handle zone (within 8 pixels) in y = [16, 24]
        if (std::abs(pos.x() - xStart) <= 8.0 && pos.y() >= 16.0 && pos.y() <= 24.0) {
            m_interaction = InteractionMode::DraggingLoopStart;
            m_dragStartLoopStart = m_loopStart;
            m_dragStartLoopEnd   = m_loopEnd;
            event->accept();
            return;
        }
        // Check Loop End handle zone (within 8 pixels) in y = [16, 24]
        if (std::abs(pos.x() - xEnd) <= 8.0 && pos.y() >= 16.0 && pos.y() <= 24.0) {
            m_interaction = InteractionMode::DraggingLoopEnd;
            m_dragStartLoopStart = m_loopStart;
            m_dragStartLoopEnd   = m_loopEnd;
            event->accept();
            return;
        }
    }

    // Click in the loop zone to draw loop range
    if (pos.y() >= 16.0 && pos.y() <= 24.0) {
        m_interaction = InteractionMode::DrawingLoopRange;
        m_dragStartLoopStart = snapFrame(xToFrame(pos.x()));
        m_loopStart = m_dragStartLoopStart;
        m_loopEnd = m_dragStartLoopStart;
        m_loopEnabled = true;
        event->accept();
        return;
    }

    // 2. Zoom / Scroll triggering modifiers
    const bool isZoomModifier = (event->modifiers() & Qt::ShiftModifier) ||
                                (event->button() == Qt::RightButton) ||
                                (event->button() == Qt::MiddleButton);

    if (isZoomModifier) {
        m_interaction    = InteractionMode::ZoomScrolling;
        m_dragStartZoom  = m_zoomFactor;
        m_dragStartFrame = m_startFrame;
    } else {
        // Default: Left click seek
        m_interaction = InteractionMode::Seeking;
        uint64_t targetFrame = xToFrame(pos.x());
        if (!(event->modifiers() & Qt::AltModifier)) {
            targetFrame = snapFrame(targetFrame);
        }
        emit seekRequested(targetFrame);
    }

    event->accept();
}

void PlaylistTimelineRuler::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();

    switch (m_interaction) {
        case InteractionMode::Seeking: {
            uint64_t targetFrame = xToFrame(pos.x());
            if (!(event->modifiers() & Qt::AltModifier)) {
                targetFrame = snapFrame(targetFrame);
            }
            emit seekRequested(targetFrame);
            break;
        }

        case InteractionMode::DrawingLoopRange: {
            uint64_t currentFrame = snapFrame(xToFrame(pos.x()));
            m_loopStart = std::min(m_dragStartLoopStart, currentFrame);
            m_loopEnd = std::max(m_dragStartLoopStart, currentFrame);
            emit loopRangeChanged(m_loopStart, m_loopEnd);
            update();
            break;
        }

        case InteractionMode::DraggingLoopStart: {
            const int64_t deltaFrames = static_cast<int64_t>((pos.x() - m_dragAnchorPos.x()) / m_zoomFactor);
            const int64_t newLoopStart = static_cast<int64_t>(m_dragStartLoopStart) + deltaFrames;
            uint64_t clampedStart = static_cast<uint64_t>(std::max(int64_t{0}, newLoopStart));
            if (!(event->modifiers() & Qt::AltModifier)) {
                clampedStart = snapFrame(clampedStart);
            }

            // Prevent crossing over loopEnd (ensure at least 1 beat spacer)
            if (clampedStart < m_loopEnd) {
                m_loopStart = clampedStart;
                emit loopRangeChanged(m_loopStart, m_loopEnd);
                update();
            }
            break;
        }

        case InteractionMode::DraggingLoopEnd: {
            const int64_t deltaFrames = static_cast<int64_t>((pos.x() - m_dragAnchorPos.x()) / m_zoomFactor);
            const int64_t newLoopEnd = static_cast<int64_t>(m_dragStartLoopEnd) + deltaFrames;
            uint64_t clampedEnd = static_cast<uint64_t>(std::max(int64_t{0}, newLoopEnd));
            if (!(event->modifiers() & Qt::AltModifier)) {
                clampedEnd = snapFrame(clampedEnd);
            }

            // Prevent crossing under loopStart
            if (clampedEnd > m_loopStart) {
                m_loopEnd = clampedEnd;
                emit loopRangeChanged(m_loopStart, m_loopEnd);
                update();
            }
            break;
        }

        case InteractionMode::ZoomScrolling: {
            const double dx = pos.x() - m_dragAnchorPos.x();
            const double dy = pos.y() - m_dragAnchorPos.y();

            // Vertical drag: adjusts Zoom Factor
            // -1 px = increase zoom (stretch grid)
            const double zoomScaleFactor = 1.0 - (dy * 0.005);
            const double newZoom = std::max(0.0001, std::min(1.0, m_dragStartZoom * zoomScaleFactor));

            // Horizontal drag: adjusts visible offset (Start Frame)
            const int64_t frameDelta = static_cast<int64_t>(dx / newZoom);
            const int64_t newStartFrame = static_cast<int64_t>(m_dragStartFrame) - frameDelta;
            const uint64_t clampedStart = static_cast<uint64_t>(std::max(int64_t{0}, newStartFrame));

            emit zoomScrollChanged(clampedStart, newZoom);
            break;
        }

        case InteractionMode::DraggingMarker: {
            uint64_t targetFrame = xToFrame(pos.x());
            if (!(event->modifiers() & Qt::AltModifier)) {
                targetFrame = snapFrame(targetFrame);
            }
            
            // Find current label and color
            for (uint32_t i = 0; i < m_markerCount; ++i) {
                if (m_markers[i].uuid == m_draggedMarkerUuid) {
                    m_timeline->updateMarker(m_markers[i].uuid, targetFrame, m_markers[i].label, m_markers[i].colorARGB);
                    break;
                }
            }
            refreshTimelineCache();
            break;
        }

        case InteractionMode::None: {
            // Adjust cursor shape dynamically for feedback
            bool nearMarker = false;
            for (uint32_t i = 0; i < m_markerCount; ++i) {
                double x = frameToX(m_markers[i].framePosition);
                if (std::abs(pos.x() - x) <= 8.0) {
                    nearMarker = true;
                    break;
                }
            }

            if (nearMarker) {
                setCursor(Qt::SizeHorCursor);
            } else if (m_loopEnabled && m_loopEnd > m_loopStart) {
                const double xStart = frameToX(m_loopStart);
                const double xEnd   = frameToX(m_loopEnd);
                if ((std::abs(pos.x() - xStart) <= 8.0 || std::abs(pos.x() - xEnd) <= 8.0) && pos.y() >= 16.0 && pos.y() <= 24.0) {
                    setCursor(Qt::SizeHorCursor);
                } else {
                    setCursor(Qt::ArrowCursor);
                }
            } else {
                setCursor(Qt::ArrowCursor);
            }
            break;
        }
    }

    event->accept();
}

void PlaylistTimelineRuler::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_interaction == InteractionMode::DraggingMarker) {
        m_interaction = InteractionMode::None;
        refreshTimelineCache();
        event->accept();
        return;
    }
    if (m_interaction == InteractionMode::DrawingLoopRange) {
        if (m_loopStart == m_loopEnd) {
            m_loopEnabled = false;
        } else {
            emit loopRangeChanged(m_loopStart, m_loopEnd);
        }
    }
    m_interaction = InteractionMode::None;
    setCursor(Qt::ArrowCursor);
    update();
    event->accept();
}

void PlaylistTimelineRuler::mouseDoubleClickEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    for (uint32_t i = 0; i < m_markerCount; ++i) {
        double x = frameToX(m_markers[i].framePosition);
        if (std::abs(pos.x() - x) <= 8.0) {
            bool ok = false;
            QString oldText = QString::fromUtf8(m_markers[i].label);
            QString text = DAWInputDialog::getText(this, QStringLiteral("Edit Marker Name / Comment"),
                                                 QStringLiteral("Marker Name/Comment:"),
                                                 oldText, &ok);
            if (ok && !text.isEmpty()) {
                m_timeline->updateMarker(m_markers[i].uuid, m_markers[i].framePosition, text.toUtf8().constData(), m_markers[i].colorARGB);
                refreshTimelineCache();
            }
            event->accept();
            return;
        }
    }
    event->ignore();
}

void PlaylistTimelineRuler::contextMenuEvent(QContextMenuEvent* event)
{
    m_interaction = InteractionMode::None; // Prevent zoom/scroll scaling when menu is dismissed
    setCursor(Qt::ArrowCursor);
    const QPoint pos = event->pos();
    
    // Check if clicking near a marker
    for (uint32_t i = 0; i < m_markerCount; ++i) {
        double x = frameToX(m_markers[i].framePosition);
        if (std::abs(pos.x() - x) <= 10.0) {
            QMenu menu(this);
            QAction* renameAct = menu.addAction(QStringLiteral("Rename Marker / Comment"));
            QAction* colorAct = menu.addAction(QStringLiteral("Change Marker Color"));
            QAction* deleteAct = menu.addAction(QStringLiteral("Delete Marker"));
            
            QAction* selected = menu.exec(event->globalPos());
            if (selected == renameAct) {
                bool ok = false;
                QString oldText = QString::fromUtf8(m_markers[i].label);
                QString text = DAWInputDialog::getText(this, QStringLiteral("Rename Marker"),
                                                     QStringLiteral("Marker Name/Comment:"),
                                                     oldText, &ok);
                if (ok && !text.isEmpty()) {
                    m_timeline->updateMarker(m_markers[i].uuid, m_markers[i].framePosition, text.toUtf8().constData(), m_markers[i].colorARGB);
                    refreshTimelineCache();
                }
            } else if (selected == colorAct) {
                QColor oldColor = QColor(m_markers[i].colorARGB);
                QColor newColor = QColorDialog::getColor(oldColor, this, QStringLiteral("Select Marker Color"));
                if (newColor.isValid()) {
                    m_timeline->updateMarker(m_markers[i].uuid, m_markers[i].framePosition, m_markers[i].label, newColor.rgba());
                    refreshTimelineCache();
                }
            } else if (selected == deleteAct) {
                m_timeline->removeMarker(m_markers[i].uuid);
                refreshTimelineCache();
            }
            event->accept();
            return;
        }
    }
    
    // Generic menu:
    QMenu menu(this);
    QAction* addMarkerAct = menu.addAction(QStringLiteral("Add Marker Here"));
    QAction* selected = menu.exec(event->globalPos());
    if (selected == addMarkerAct) {
        uint64_t targetFrame = xToFrame(pos.x());
        if (!(event->modifiers() & Qt::AltModifier)) {
            targetFrame = snapFrame(targetFrame);
        }
        
        bool ok = false;
        QString text = DAWInputDialog::getText(this, QStringLiteral("Add Marker"),
                                             QStringLiteral("Marker Name/Comment:"),
                                             QStringLiteral("Marker"), &ok);
        if (ok && !text.isEmpty()) {
            m_timeline->addMarker(targetFrame, text.toUtf8().constData(), 0xFF00FFCC);
            refreshTimelineCache();
        }
    }
    event->accept();
}

void PlaylistTimelineRuler::wheelEvent(QWheelEvent* event)
{
    // Enable simple zoom scrolling on the ruler using wheel events directly
    // Shift+Wheel or horizontal axis = scroll horizontal
    // Ctrl+Wheel = zoom
    const bool isZoom = event->modifiers() & Qt::ControlModifier;
    const QPoint numPixels = event->pixelDelta();
    const QPoint numDegrees = event->angleDelta() / 8;

    double deltaX = 0.0;
    double deltaY = 0.0;

    if (!numPixels.isNull()) {
        deltaX = static_cast<double>(numPixels.x());
        deltaY = static_cast<double>(numPixels.y());
    } else if (!numDegrees.isNull()) {
        deltaX = static_cast<double>(numDegrees.x()) * 1.5;
        deltaY = static_cast<double>(numDegrees.y()) * 1.5;
    }

    if (isZoom) {
        // Zoom centered around current cursor position
        const double mouseX = event->position().x();
        const uint64_t cursorFrame = xToFrame(mouseX);

        const double zoomScale = 1.0 + (deltaY * 0.002);
        const double newZoom = std::max(0.0001, std::min(1.0, m_zoomFactor * zoomScale));

        // Re-anchor start frame so cursor remains over the exact same musical position
        const double newStartFrameD = static_cast<double>(cursorFrame) - (mouseX / newZoom);
        const uint64_t clampedStart = static_cast<uint64_t>(std::max(0.0, newStartFrameD));

        emit zoomScrollChanged(clampedStart, newZoom);
    } else {
        // Plain scroll
        const double scrollFactor = (event->modifiers() & Qt::ShiftModifier) ? 5.0 : 1.0;
        const int64_t frameDelta  = static_cast<int64_t>((deltaX - deltaY) * 2.0 * scrollFactor / m_zoomFactor);
        const int64_t newStart    = static_cast<int64_t>(m_startFrame) + frameDelta;
        const uint64_t clampedStart = static_cast<uint64_t>(std::max(int64_t{0}, newStart));

        emit zoomScrollChanged(clampedStart, m_zoomFactor);
    }

    event->accept();
}

uint64_t PlaylistTimelineRuler::snapFrame(uint64_t frame) const
{
    if (!m_inputMode || !m_timeline) {
        return frame;
    }

    const bridge::SnapMode mode = m_inputMode->getSnapMode();
    if (mode == bridge::SnapMode::Free) {
        return frame;
    }

    if (mode == bridge::SnapMode::Bar) {
        uint32_t bar, beat, tick;
        m_timeline->frameToBBT(frame, bar, beat, tick);

        const uint64_t currentBarFrame = m_timeline->bbtToFrame(bar, 1, 0);
        const uint64_t nextBarFrame = m_timeline->bbtToFrame(bar + 1, 1, 0);

        return (frame - currentBarFrame < nextBarFrame - frame) ? currentBarFrame : nextBarFrame;
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

} // namespace presentation::views
