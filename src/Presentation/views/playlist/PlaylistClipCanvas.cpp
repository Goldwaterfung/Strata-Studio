// src/Presentation/views/playlist/PlaylistClipCanvas.cpp
#include "PlaylistClipCanvas.h"

#include <QPainter>
#include <QToolTip>
#include <QHelpEvent>
#include <QDebug>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QLinearGradient>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstring>

#include "Middle Bridge/browser/dnd_primitives.h"
#include "Middle Bridge/automation/automation_helpers.h"
#include "common/math/gain.h"
#include "clips/AudioClipItem.h"
#include "playlist/waveform/TileRenderWorker.h"
#include "playlist/waveform/WaveformTileCache.h"
#include "clips/PatternClipItem.h"
#include "clips/AutomationClipItem.h"
#include "theme.h"
#include "PlaylistContextMenu.h"
#include "dialogs/DAWInputDialog.h"
#include "common/system_primitives.h"

namespace presentation::views {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

PlaylistClipCanvas::PlaylistClipCanvas(
    bridge::IArrangementController* arrangement,
    bridge::ITimelineController*    timeline,
    bridge::IWaveformCacheProvider* waveform,
    bridge::IAutomationController*  automation,
    bridge::IPatternDataProvider*   patternData,
    bridge::IBrowserController*     browser,
    bridge::IInputModeController*   inputMode,
    QWidget* parent)
    : QWidget(parent)
    , m_arrangement(arrangement)
    , m_timeline(timeline)
    , m_waveform(waveform)
    , m_automation(automation)
    , m_patternData(patternData)
    , m_browser(browser)
    , m_inputMode(inputMode)
    , m_dragRegionId(bridge::RegionID::invalid())
{
    setObjectName(QStringLiteral("PlaylistClipCanvas"));
    setMouseTracking(true);   // Needed for hover cursor feedback
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    // Dark background — clips paint on top
    setAutoFillBackground(false);

    m_tileWorker = std::make_unique<TileRenderWorker>(waveform, this);
    connect(m_tileWorker.get(), &TileRenderWorker::tileRendered,
            this, &PlaylistClipCanvas::onTileRendered,
            Qt::QueuedConnection);
    m_tileWorker->start();
    m_tileCache.setProvider(waveform);

    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, [this]() {
        update();
    });

    m_heightDebounceTimer.setSingleShot(true);
    connect(&m_heightDebounceTimer, &QTimer::timeout, this, [this]() {
        if (!m_view.trackLayouts.empty()) {
            m_tileCache.clear();
            prefetchWaveformTiles();
            update();
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// State setters (called from PlaylistWindow)
// ─────────────────────────────────────────────────────────────────────────────

void PlaylistClipCanvas::setViewState(const ViewState& vs)
{
    const bool viewChanged = (vs.viewStartFrame != m_view.viewStartFrame || vs.viewEndFrame != m_view.viewEndFrame);
    const bool zoomChanged = (vs.zoomFactor != m_view.zoomFactor);
    const bool layoutChanged = (vs.trackLayouts != m_view.trackLayouts || vs.defaultTrackHeight != m_view.defaultTrackHeight);

    if (viewChanged || zoomChanged || vs.trackCount != m_view.trackCount || layoutChanged || vs.verticalOffsetPx != m_view.verticalOffsetPx)
    {
        // TileKey omits height: (mediaId, zoomTier, tileX, colorARGB, generation).
        // Cached tiles stretch/compress correctly via drawPixmap during height drag.
        // Cache is bounded by WaveformTileCache::MAX_TILES (500) with LRU eviction.
        // Clearing on layoutChanged would nuke tiles on every mouse-move during height
        // drag, causing continuous flicker as tiles never finish rendering.

        m_view = vs;
        m_cachedPlayheadX = frameToX(m_playheadFrame);

        // Debounced tile re-render after pure height-drag settles.
        // During drag, cached tiles (at old height) stretch via drawPixmap — no flicker.
        // Once drag stops for 200ms, clear cache and re-render tiles at the final height
        // so the waveform is crisp without waiting for next zoom/scroll interaction.
        if (layoutChanged && !viewChanged && !zoomChanged) {
            m_heightDebounceTimer.start(200);
        }

        if (viewChanged || zoomChanged || layoutChanged) {
            prefetchWaveformTiles();
        }

        update();
    }
}

void PlaylistClipCanvas::setTrackList(const std::vector<bridge::TrackUIState>& tracks)
{
    m_tracks = tracks;
    update();
}

void PlaylistClipCanvas::setPlayheadFrame(uint64_t frame)
{
    if (frame == m_playheadFrame) {
        return;
    }
    m_playheadFrame = frame;

    const double newX = frameToX(frame);
    // Only repaint if pixel position changed by at least 1 px
    if (std::abs(newX - m_cachedPlayheadX) >= 1.0) {
        // Calculate old playhead rect (3px left, 6px width total)
        QRectF oldRect(m_cachedPlayheadX - 3.0, 0.0, 6.0, static_cast<double>(height()));
        
        m_cachedPlayheadX = newX;
        
        // Calculate new playhead rect
        QRectF newRect(m_cachedPlayheadX - 3.0, 0.0, 6.0, static_cast<double>(height()));
        
        update(oldRect.toAlignedRect());
        update(newRect.toAlignedRect());
    }
}

void PlaylistClipCanvas::clearAll()
{
    m_tracks.clear();
    m_playheadFrame    = 0;
    m_cachedPlayheadX  = -1.0;
    m_dragState        = CanvasDragState::Idle;
    m_rubberBandVisible = false;
    m_selectedRegions.clear();
    m_lastSelectedRegionId = bridge::RegionID::invalid();
    m_tileCache.clear();
    m_inFlightTiles.clear();
    m_retryTimer.stop();
    update();
}

bool PlaylistClipCanvas::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* helpEvent = static_cast<QHelpEvent*>(event);
        if (m_arrangement) {
            const QPoint pos = helpEvent->pos();
            bridge::VisualRegion regions[MAX_VISIBLE];
            const uint32_t count = m_arrangement->getRegionsInViewport(
                m_view.viewStartFrame, m_view.viewEndFrame, regions, MAX_VISIBLE);

            bool found = false;
            for (uint32_t i = 0; i < count; ++i) {
                QRectF rect = regionToRect(regions[i]);
                if (rect.contains(pos)) {
                    if (regions[i].hasCustomComment && std::strlen(regions[i].comment) > 0) {
                        QToolTip::showText(helpEvent->globalPos(), QString::fromUtf8(regions[i].comment), this);
                        found = true;
                    }
                    break;
                }
            }
            if (!found) {
                QToolTip::hideText();
                event->ignore();
            } else {
                event->accept();
            }
            return true;
        }
    }
    return QWidget::event(event);
}

// ─────────────────────────────────────────────────────────────────────────────
// paintEvent — zero allocation, reads member vars only
// ─────────────────────────────────────────────────────────────────────────────

void PlaylistClipCanvas::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // --- 1. Background and track lane rows ---
    drawBackground(p);
    drawTrackLanes(p);
    drawGridLines(p);

    // --- 2. Fetch and draw visible clips (stack buffer, no heap) ---
    if (m_arrangement && m_view.viewEndFrame > m_view.viewStartFrame) {
        // Stack-allocated output buffer (plan §2-A critical rule)
        bridge::VisualRegion regions[MAX_VISIBLE];
        const uint32_t count = m_arrangement->getRegionsInViewport(
            m_view.viewStartFrame, m_view.viewEndFrame, regions, MAX_VISIBLE);

        for (uint32_t i = 0; i < count; ++i) {
            regions[i].isSelected = (m_selectedRegions.find(regions[i].id.toRaw()) != m_selectedRegions.end());

            bridge::VisualRegion drawRegion = regions[i];

            // Apply real-time visual snap preview during drag & resize
            bool isPartofSelection = (m_selectedRegions.find(drawRegion.id.toRaw()) != m_selectedRegions.end());
            if (m_dragState == CanvasDragState::DraggingClip && isPartofSelection) {
                const int64_t rawNewStart = static_cast<int64_t>(drawRegion.startFrame) + m_dragFrameDelta;
                const uint64_t snappedStart = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawNewStart)));
                drawRegion.startFrame = snappedStart;

                if (m_dragDestTrackIndex >= 0 && m_dragDestTrackIndex < static_cast<int>(m_tracks.size()) &&
                    m_dragOrigTrackIndex >= 0 && m_dragOrigTrackIndex < static_cast<int>(m_tracks.size()) && !m_dragHoveringEmptySpace) {
                    
                    int rTrackIdx = -1;
                    for (size_t t = 0; t < m_tracks.size(); ++t) {
                        if (m_tracks[t].trackId == drawRegion.trackId) {
                            rTrackIdx = static_cast<int>(t);
                            break;
                        }
                    }
                    if (rTrackIdx != -1) {
                        int newTrackIdx = std::clamp(rTrackIdx + (m_dragDestTrackIndex - m_dragOrigTrackIndex), 0, static_cast<int>(m_tracks.size()) - 1);
                        drawRegion.trackId = m_tracks[static_cast<size_t>(newTrackIdx)].trackId;
                    }
                }
                if (m_dragDestLayerIndex != 0xFFFFFFFF && m_dragOrigLayerIndex != 0xFFFFFFFF) {
                    int newLayer = static_cast<int>(drawRegion.layerIndex) + (static_cast<int>(m_dragDestLayerIndex) - static_cast<int>(m_dragOrigLayerIndex));
                    drawRegion.layerIndex = static_cast<uint32_t>(std::max(0, newLayer));
                }
            } else if (m_dragState == CanvasDragState::ResizingClipRight && isPartofSelection) {
                if (m_stretchMode) {
                    if (drawRegion.durationFrames > 0) {
                        const int64_t rawNewEnd = static_cast<int64_t>(drawRegion.startFrame + drawRegion.durationFrames) + m_dragFrameDelta;
                        const uint64_t snappedEnd = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawNewEnd)));
                        if (snappedEnd > drawRegion.startFrame) {
                            const uint64_t snappedDuration = snappedEnd - drawRegion.startFrame;
                            drawRegion.durationFrames = snappedDuration;
                            const double ratio = static_cast<double>(drawRegion.playbackRatio) * (static_cast<double>(drawRegion.durationFrames) / static_cast<double>(snappedDuration));
                            drawRegion.playbackRatio = static_cast<float>(ratio);
                        }
                    }
                } else {
                    const int64_t rawNewEnd = static_cast<int64_t>(drawRegion.startFrame + drawRegion.durationFrames) + m_dragFrameDelta;
                    const uint64_t snappedEnd = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawNewEnd)));
                    if (snappedEnd > drawRegion.startFrame) {
                        drawRegion.durationFrames = snappedEnd - drawRegion.startFrame;
                    }
                }
            } else if (m_dragState == CanvasDragState::ResizingClipLeft && isPartofSelection) {
                if (m_stretchMode) {
                    if (drawRegion.durationFrames > 0) {
                        const int64_t rawNewStart = static_cast<int64_t>(drawRegion.startFrame) + m_dragFrameDelta;
                        const uint64_t snappedStart = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawNewStart)));
                        const uint64_t endFrame = drawRegion.startFrame + drawRegion.durationFrames;
                        if (snappedStart < endFrame) {
                            const uint64_t snappedDuration = endFrame - snappedStart;
                            drawRegion.startFrame = snappedStart;
                            drawRegion.durationFrames = snappedDuration;
                            const double ratio = static_cast<double>(drawRegion.playbackRatio) * (static_cast<double>(drawRegion.durationFrames) / static_cast<double>(snappedDuration));
                            drawRegion.playbackRatio = static_cast<float>(ratio);
                        }
                    }
                } else {
                    const int64_t rawNewStart = static_cast<int64_t>(drawRegion.startFrame) + m_dragFrameDelta;
                    const uint64_t snappedStart = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawNewStart)));
                    const uint64_t endFrame = drawRegion.startFrame + drawRegion.durationFrames;
                    if (snappedStart < endFrame) {
                        drawRegion.startFrame = snappedStart;
                        drawRegion.durationFrames = endFrame - snappedStart;
                    }
                }
            } else if (m_dragState == CanvasDragState::DraggingFadeIn && isPartofSelection) {
                const int64_t rawFadeIn = static_cast<int64_t>(drawRegion.fadeInFrames) + m_dragFadeDelta;
                const int64_t maxFade = static_cast<int64_t>(drawRegion.durationFrames);
                drawRegion.fadeInFrames = static_cast<uint32_t>(std::clamp(rawFadeIn, int64_t{0}, maxFade));
            } else if (m_dragState == CanvasDragState::DraggingFadeOut && isPartofSelection) {
                const int64_t rawFadeOut = static_cast<int64_t>(drawRegion.fadeOutFrames) + m_dragFadeDelta;
                const int64_t maxFade = static_cast<int64_t>(drawRegion.durationFrames);
                drawRegion.fadeOutFrames = static_cast<uint32_t>(std::clamp(rawFadeOut, int64_t{0}, maxFade));
            } else if (m_dragState == CanvasDragState::DraggingGainBadge && isPartofSelection) {
                drawRegion.gainLinear = std::max(0.0f, drawRegion.gainLinear + m_dragGainDelta);
            }

            const QRectF clipRect = regionToRect(drawRegion);
            if (clipRect.width() < 1.0) {
                continue;
            }

            // All clips are fully visible (opacity 1.0) in the modeless layout
            p.setOpacity(1.0);

            switch (drawRegion.clipType) {
                case composition::RegionType::AUDIO:
                    AudioClipItem::paint(p, clipRect, drawRegion, m_waveform,
                                         m_view.viewStartFrame, m_view.viewEndFrame, m_view.zoomFactor,
                                         &m_tileCache, m_tileWorker.get(), m_inFlightTiles);
                    break;
                case composition::RegionType::MIDI:
                    PatternClipItem::paint(p, clipRect, drawRegion, m_patternData, m_noteColorMode);
                    break;
                case composition::RegionType::AUTOMATION:
                    break;
                default:
                    break;
            }
        }
        p.setOpacity(1.0);

        // --- 2b. Draw Active Recordings ---
        bridge::IArrangementController::VisualActiveRecording recordings[32];
        const uint32_t recCount = m_arrangement->getActiveRecordings(recordings, 32);
        for (uint32_t i = 0; i < recCount; ++i) {
            const auto& rec = recordings[i];
            
            int row = -1;
            for (int r = 0; r < static_cast<int>(m_tracks.size()); ++r) {
                if (m_tracks[static_cast<size_t>(r)].trackId.toRaw() == rec.trackId.toRaw()) {
                    row = r;
                    break;
                }
            }
            if (row < 0 || row >= static_cast<int>(m_view.trackLayouts.size())) {
                continue;
            }

            const auto& layout = m_view.trackLayouts[static_cast<size_t>(row)];
            const double yOffset = getTrackYOffset(row) - static_cast<double>(m_view.verticalOffsetPx);
            const double rowHeight = layout.mainLaneHeight;

            const double startX = frameToX(rec.startFrame);
            const double endX = frameToX(rec.currentFrame);
            
            if (endX > startX) {
                QRectF recRect(startX, yOffset, endX - startX, rowHeight);
                p.setOpacity(0.4);
                p.fillRect(recRect, QColor(255, 0, 0));

                if (rec.livePeaks && rec.numPeaks > 0) {
                    p.setOpacity(0.8);
                    p.setPen(QPen(QColor(255, 255, 255, 180), 1.0));
                    
                    const double centerY = yOffset + rowHeight * 0.5;
                    const double maxAmp = rowHeight * 0.5;
                    
                    QPainterPath wavePath;
                    bool started = false;
                    for (uint32_t pIdx = 0; pIdx < rec.numPeaks; ++pIdx) {
                        uint64_t frame = rec.startFrame + pIdx * 64;
                        if (frame > m_view.viewEndFrame) break;
                        if (frame + 64 < m_view.viewStartFrame) continue;
                        
                        double px = frameToX(frame);
                        double amp = std::min(1.0, static_cast<double>(rec.livePeaks[pIdx])) * maxAmp;
                        
                        if (!started) {
                            wavePath.moveTo(px, centerY - amp);
                            started = true;
                        } else {
                            wavePath.lineTo(px, centerY - amp);
                        }
                    }
                    
                    if (started) {
                        for (uint32_t pIdxBack = rec.numPeaks; pIdxBack > 0; --pIdxBack) {
                            uint32_t pIdx = pIdxBack - 1;
                            uint64_t frame = rec.startFrame + pIdx * 64;
                            if (frame > m_view.viewEndFrame) continue;
                            if (frame + 64 < m_view.viewStartFrame) continue;
                            
                            double px = frameToX(frame);
                            double amp = std::min(1.0, static_cast<double>(rec.livePeaks[pIdx])) * maxAmp;
                            wavePath.lineTo(px, centerY + amp);
                        }
                        
                        p.setBrush(QColor(255, 255, 255, 100));
                        p.drawPath(wavePath);
                    }
                }

                p.setOpacity(1.0);
                p.setPen(QPen(QColor(255, 50, 50), 1.0));
                p.drawRect(recRect);
            }
        }
        p.setOpacity(1.0);
    }

    // --- 3. Rubber-band selection / new-clip outline ---
    if (m_rubberBandVisible) {
        drawRubberBand(p);
    }
    
    // --- 3.5 Comping Highlight ---
    drawCompHighlight(p);

    // --- 4. Playhead (always drawn last — on top of clips) ---
    drawPlayhead(p);

    // --- 5. HUD tooltip (Bug 4) — drawn above everything ---
    drawHUD(p);
}

// ─────────────────────────────────────────────────────────────────────────────
// Paint helpers
// ─────────────────────────────────────────────────────────────────────────────

void PlaylistClipCanvas::drawBackground(QPainter& p)
{
    p.fillRect(rect(), theme::Color::BgBase);
}

void PlaylistClipCanvas::drawTrackLanes(QPainter& p)
{
    const uint32_t trackCount = m_view.trackCount;
    const double   w          = static_cast<double>(width());

    double y = -static_cast<double>(m_view.verticalOffsetPx);
    for (uint32_t i = 0; i < trackCount; ++i) {
        if (i >= m_view.trackLayouts.size()) {
            break;
        }
        const auto& layout = m_view.trackLayouts[i];
        const double laneH = layout.totalHeight;
        QRectF lane(0.0, y, w, laneH);

        // Alternate row shading for visual separation
        const QColor laneBg = (i % 2 == 0)
            ? theme::Color::BgSurface
            : theme::Color::BgBase;

        p.fillRect(lane, laneBg);

        // Bottom separator line
        QColor sepColor = theme::Color::BgControl;
        sepColor.setAlpha(200);
        p.setPen(QPen(sepColor, 1.0));
        p.drawLine(QPointF(0.0, y + laneH - 0.5),
                   QPointF(w,   y + laneH - 0.5));

        if (i < m_tracks.size()) {
            const auto& track = m_tracks[i];
            
            if (layout.isTakesExpanded && layout.audioLanesCount > 1) {
                double takesOffset = layout.mainLaneHeight;
                for (uint32_t j = 1; j < layout.audioLanesCount; ++j) {
                    double takesLaneH = (j < layout.takesLaneHeights.size()) ? layout.takesLaneHeights[j] : 60.0;
                    p.setPen(QPen(sepColor, 1.0));
                    p.drawLine(QPointF(0.0, y + takesOffset - 0.5),
                               QPointF(w,   y + takesOffset - 0.5));
                    
                    QRectF takesLaneBg(0.0, y + takesOffset, w, takesLaneH);
                    p.fillRect(takesLaneBg, theme::Color::BgBase);

                    takesOffset += takesLaneH;
                }
                p.setPen(QPen(sepColor, 1.0));
                p.drawLine(QPointF(0.0, y + takesOffset - 0.5),
                           QPointF(w,   y + takesOffset - 0.5));
            }

            if (!layout.subLanes.empty()) {
                double mainH = layout.mainLaneHeight;
                p.setPen(QPen(sepColor, 1.0));
                p.drawLine(QPointF(0.0, y + mainH - 0.5),
                           QPointF(w,   y + mainH - 0.5));

                for (const auto& sl : layout.subLanes) {
                    if (!sl.isExpanded) continue;
                    double subH = sl.height;
                    double currentSubY = y + sl.relativeOffset;
                    QRectF subLane(0.0, currentSubY, w, subH);
                    p.fillRect(subLane, theme::Color::BgBase);
                    p.setPen(QPen(sepColor, 1.0));
                    p.drawLine(QPointF(0.0, currentSubY + subH - 0.5),
                               QPointF(w,   currentSubY + subH - 0.5));

                    // Render continuous automation line on the sub-lane
                    if (m_automation && m_automation->isAutomationVisible(track.trackId)) {
                        const bool editable = m_automation->isAutomationWriteEnabled(track.trackId);
                        const uint64_t startFrame = m_view.viewStartFrame;
                        const uint64_t endFrame = m_view.viewEndFrame;
                        const uint64_t durationFrames = (endFrame > startFrame) ? (endFrame - startFrame) : 1;
                        
                        double defaultVal = 0.5;
                        if (m_automation) {
                            defaultVal = static_cast<double>(m_automation->getBaseParameterValue(sl.targetNodeId, sl.subNodeId, sl.parameterIndex));
                        }

                        static constexpr uint32_t MAX_AUTO_PTS = 2048;
                        bridge::VisualAutomationPoint autoPts[MAX_AUTO_PTS];
                        uint32_t autoPtsCount = m_automation->getCurvePoints(
                            track.trackId,
                            sl.targetNodeId,
                            sl.subNodeId,
                            sl.parameterIndex,
                            startFrame,
                            endFrame,
                            autoPts,
                            MAX_AUTO_PTS
                        );

                        const QRectF innerRect(
                            subLane.left()   + 1.0,
                            subLane.top()    + 1.0,
                            subLane.width()  - 2.0,
                            subLane.height() - 2.0
                        );

                        if (innerRect.height() > 4.0) {
                            p.save();
                            p.setClipRect(subLane);
                            AutomationClipItem::drawCurve(p, innerRect, autoPts, autoPtsCount,
                                                          startFrame, durationFrames, editable, defaultVal);
                            if (editable) {
                                AutomationClipItem::drawControlPoints(p, innerRect, autoPts, autoPtsCount,
                                                                      startFrame, durationFrames);
                            }
                            p.restore();
                        }
                    }
                }
            }
        }

        // Draw glowing highlights on compatible lanes if active drag is happening
        if (m_activeDragType != -1 && i < m_tracks.size()) {
            const auto& track = m_tracks[static_cast<size_t>(i)];
            bool compatible = false;
            if (m_activeDragType == static_cast<int>(bridge::BrowserItemType::AudioFile)) {
                compatible = (track.type == composition::TrackType::AUDIO);
            } else if (m_activeDragType == static_cast<int>(bridge::BrowserItemType::MidiFile)) {
                compatible = (track.type == composition::TrackType::MIDI || track.type == composition::TrackType::INSTRUMENT);
            }
            if (compatible) {
                p.save();
                QColor dragPen = theme::Color::AccentGlow;
                dragPen.setAlpha(150);
                p.setPen(QPen(dragPen, 1.0));
                p.drawRect(lane.adjusted(0.5, 0.5, -0.5, -0.5));
                p.restore();
            }
        }

        y += laneH;
    }

    // Draw ghost track row below all tracks if dragging compatible items in empty space
    if (m_dragHoveringEmptySpace && m_activeDragType != -1) {
        bool showGhost = false;
        if (m_activeDragType == static_cast<int>(bridge::BrowserItemType::AudioFile) ||
            m_activeDragType == static_cast<int>(bridge::BrowserItemType::PluginGenerator)) {
            showGhost = true;
        }

        if (showGhost) {
            const double ghostH = 72.0;
            QRectF ghostLane(0.0, y, w, ghostH);

            p.save();
            QColor ghostBg = theme::Color::AccentGlow;
            ghostBg.setAlpha(15);
            p.fillRect(ghostLane, ghostBg);
            QColor ghostPen = theme::Color::AccentGlow;
            ghostPen.setAlpha(150);
            QPen pen(ghostPen, 1.0, Qt::DashLine);
            p.setPen(pen);
            p.drawRect(ghostLane.adjusted(0.5, 0.5, -0.5, -0.5));
            QColor textPen = theme::Color::AccentGlow;
            textPen.setAlpha(200);
            p.setPen(QPen(textPen));
            p.setFont(theme::Font::primary(9, QFont::Bold));
            p.drawText(ghostLane, Qt::AlignCenter, QStringLiteral("Drop here to create a new track"));
            p.restore();
        }
    }
}

void PlaylistClipCanvas::drawGridLines(QPainter& p)
{
    if (m_view.viewEndFrame <= m_view.viewStartFrame || m_view.zoomFactor <= 0.0 || !m_timeline) {
        return;
    }

    uint32_t startBar, startBeat, startTick;
    m_timeline->frameToBBT(m_view.viewStartFrame, startBar, startBeat, startTick);

    // Estimate frames per bar at start to determine barStep density
    uint64_t currentBarFrame = m_timeline->bbtToFrame(startBar, 1, 0);
    uint64_t nextBarFrame = m_timeline->bbtToFrame(startBar + 1, 1, 0);
    double estFramesPerBar = static_cast<double>(nextBarFrame > currentBarFrame ? nextBarFrame - currentBarFrame : 48000.0 * 2.0);

    uint32_t barStep = 1;
    while (estFramesPerBar * static_cast<double>(barStep) * m_view.zoomFactor < 40.0) {
        barStep *= 2;
    }

    const double widgetH       = static_cast<double>(height());

    for (uint32_t bar = startBar; ; bar += barStep) {
        uint64_t barFrame = m_timeline->bbtToFrame(bar, 1, 0);
        if (barFrame > m_view.viewEndFrame) {
            break;
        }

        if (barFrame >= m_view.viewStartFrame) {
            const double x = frameToX(barFrame);
            QColor barLineColor = theme::Color::BgControl;
            barLineColor.setAlpha(200);
            p.setPen(QPen(barLineColor, 1.0)); // Lighter gray for bar lines
            p.drawLine(QPointF(x, 0.0), QPointF(x, widgetH));
        }

        // Only draw beats if we're not skipping bars
        if (barStep == 1) {
            uint8_t num, den;
            m_timeline->getTimeSignatureAtFrame(barFrame, num, den);
            
            // Check spacing of a beat to see if it's too packed
            uint64_t nextBeatFrame = m_timeline->bbtToFrame(bar, 2, 0);
            double beatWidthPx = static_cast<double>(nextBeatFrame > barFrame ? nextBeatFrame - barFrame : 0) * m_view.zoomFactor;

            if (beatWidthPx >= 12.0) {
                for (uint32_t beat = 2; beat <= num; ++beat) {
                    uint64_t beatFrame = m_timeline->bbtToFrame(bar, beat, 0);
                    if (beatFrame > m_view.viewEndFrame) {
                        break;
                    }
                    if (beatFrame >= m_view.viewStartFrame) {
                        const double x = frameToX(beatFrame);
                        QColor beatLineColor = theme::Color::BgControl;
                        beatLineColor.setAlpha(120);
                        p.setPen(QPen(beatLineColor, 1.0)); // Subtler beat lines
                        p.drawLine(QPointF(x, 0.0), QPointF(x, widgetH));
                    }
                }
            }
        }
    }
}

void PlaylistClipCanvas::drawPlayhead(QPainter& p)
{
    if (m_cachedPlayheadX < 0.0 || m_cachedPlayheadX > static_cast<double>(width())) {
        return;
    }

    const double h = static_cast<double>(height());

    // Subtle glow halo
    QLinearGradient glow(m_cachedPlayheadX - 3.0, 0.0,
                         m_cachedPlayheadX + 3.0, 0.0);
    glow.setColorAt(0.0, QColor(0xFF, 0x3B, 0x30, 0));
    glow.setColorAt(0.5, QColor(0xFF, 0x3B, 0x30, 80));
    glow.setColorAt(1.0, QColor(0xFF, 0x3B, 0x30, 0));
    p.fillRect(QRectF(m_cachedPlayheadX - 3.0, 0.0, 6.0, h), glow);

    // Solid 1 px line
    p.setPen(QPen(QColor(0xFF, 0x3B, 0x30, 220), 1.0));
    p.drawLine(QPointF(m_cachedPlayheadX, 0.0),
               QPointF(m_cachedPlayheadX, h));
}

void PlaylistClipCanvas::drawCompHighlight(QPainter& p)
{
    if (!hasCompHighlight()) return;
    
    // Determine start and end X
    uint64_t start = std::min(m_compHighlightStartFrame, m_compHighlightEndFrame);
    uint64_t end = std::max(m_compHighlightStartFrame, m_compHighlightEndFrame);
    
    double startX = frameToX(start);
    double endX = frameToX(end);
    double w = endX - startX;
    
    // Find track layout
    int trackIdx = -1;
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].trackId == m_compHighlightTrack) {
            trackIdx = static_cast<int>(i);
            break;
        }
    }
    
    if (trackIdx < 0 || trackIdx >= static_cast<int>(m_view.trackLayouts.size())) return;
    
    const auto& layout = m_view.trackLayouts[static_cast<size_t>(trackIdx)];
    
    // Calculate Y offset
    double y = getTrackYOffset(trackIdx) - static_cast<double>(m_view.verticalOffsetPx);
    double h = 0.0;
    
    if (m_compHighlightClipType == composition::RegionType::AUTOMATION) {
        // Automation sub-lane geometry — use same helpers as drawTrackLanes()
        const double offset = layout.getSubLaneOffsetForParam(
            m_compHighlightNodeId, 0, m_compHighlightParamIndex);
        const double subH = layout.getSubLaneHeightForParam(
            m_compHighlightNodeId, 0, m_compHighlightParamIndex);
        if (offset < 0.0 || subH <= 0.0) return;  // sub-lane not currently visible
        y += offset;
        h  = subH;
    } else if (!layout.isTakesExpanded || m_compHighlightLane <= 0) {
        h = layout.mainLaneHeight;
    } else {
        double takesOffset = 0.0;
        for (int s = 1; s < m_compHighlightLane; ++s) {
            if (s < static_cast<int>(layout.takesLaneHeights.size())) {
                takesOffset += layout.takesLaneHeights[static_cast<size_t>(s)];
            }
        }
        y += layout.mainLaneHeight + takesOffset;
        h = (m_compHighlightLane < static_cast<int>(layout.takesLaneHeights.size())) 
            ? layout.takesLaneHeights[static_cast<size_t>(m_compHighlightLane)] : 60.0;
    }
    
    QRectF highlightRect(startX, y, w, h);
    
    p.setPen(QPen(theme::Color::AccentGlow, 1.0));
    QColor fill = theme::Color::AccentGlow;
    fill.setAlpha(60); // Translucent blueish highlight
    p.setBrush(fill);
    
    p.drawRect(highlightRect);
}

void PlaylistClipCanvas::drawRubberBand(QPainter& p)
{
    if (!m_rubberBandVisible || m_rubberBandRect.isNull()) {
        return;
    }

    QColor penColor = theme::Color::AccentGlow;
    penColor.setAlpha(180);
    QColor brushColor = theme::Color::AccentGlow;
    brushColor.setAlpha(18);

    p.setPen(QPen(penColor, 1.0, Qt::DashLine));
    p.setBrush(brushColor);
    p.drawRect(m_rubberBandRect);
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry helpers
// ─────────────────────────────────────────────────────────────────────────────

double PlaylistClipCanvas::getTrackYOffset(int trackIndex) const
{
    double y = 0.0;
    int limit = std::min(trackIndex, static_cast<int>(m_view.trackLayouts.size()));
    for (int i = 0; i < limit; ++i) {
        y += m_view.trackLayouts[static_cast<size_t>(i)].totalHeight;
    }
    return y;
}

QRectF PlaylistClipCanvas::regionToRect(const bridge::VisualRegion& region) const
{
    if (m_view.viewEndFrame <= m_view.viewStartFrame || m_view.zoomFactor <= 0.0) {
        return {};
    }

    // Map frame range → pixel X range
    const double x = frameToX(region.startFrame);
    const double w = static_cast<double>(region.durationFrames) * m_view.zoomFactor;

    // Find the track row
    int trackRow = -1;
    for (int t = 0; t < static_cast<int>(m_tracks.size()); ++t) {
        if (m_tracks[static_cast<size_t>(t)].trackId == region.trackId) {
            trackRow = t;
            break;
        }
    }

    if (trackRow < 0 || trackRow >= static_cast<int>(m_view.trackLayouts.size())) {
        return {};
    }

    const auto& layout = m_view.trackLayouts[static_cast<size_t>(trackRow)];
    double y = getTrackYOffset(trackRow) - static_cast<double>(m_view.verticalOffsetPx);
    double h;

    if (region.clipType == composition::RegionType::AUTOMATION) {
        double offset = layout.getSubLaneOffsetForParam(region.automationTargetNodeId, 0, region.automationParameterIndex);
        double subH = layout.getSubLaneHeightForParam(region.automationTargetNodeId, 0, region.automationParameterIndex);
        if (offset >= 0.0) {
            y += offset;
        } else {
            y += layout.mainLaneHeight;
        }
        h = subH - 1.0;
    } else {
        if (!layout.isTakesExpanded || region.layerIndex == 0) {
            // Drawn in the main lane (either takes are collapsed, or it's the main take)
            h = layout.mainLaneHeight - 1.0;
        } else {
            // Drawn in a take sub-lane
            double takesOffset = 0.0;
            for (uint32_t s = 1; s < region.layerIndex; ++s) {
                if (s < layout.takesLaneHeights.size()) {
                    takesOffset += layout.takesLaneHeights[s];
                }
            }
            y += layout.mainLaneHeight + takesOffset;
            h = (region.layerIndex < layout.takesLaneHeights.size()) ? layout.takesLaneHeights[region.layerIndex] - 1.0 : 60.0 - 1.0;
        }
    }

    return QRectF(x, y, w, h);
}

int PlaylistClipCanvas::yToTrackIndex(double y) const
{
    double accum = -static_cast<double>(m_view.verticalOffsetPx);
    for (size_t i = 0; i < m_view.trackLayouts.size(); ++i) {
        double h = m_view.trackLayouts[i].totalHeight;
        if (y < accum + h) return static_cast<int>(i);
        accum += h;
    }
    return -1;
}

void PlaylistClipCanvas::yToTrackAndLayer(double y, int& outTrackIndex, uint32_t& outLayer) const
{
    outTrackIndex = -1;
    outLayer = 0xFFFFFFFF; // AUTO_LAYER

    double accum = -static_cast<double>(m_view.verticalOffsetPx);
    for (size_t i = 0; i < m_view.trackLayouts.size(); ++i) {
        const auto& layout = m_view.trackLayouts[i];
        if (y >= accum && y < accum + layout.totalHeight) {
            outTrackIndex = static_cast<int>(i);
            if (layout.isTakesExpanded) {
                double laneAccum = accum + layout.mainLaneHeight;
                if (y < laneAccum) {
                    outLayer = 0; // Main lane
                    return;
                }
                for (size_t j = 1; j < layout.takesLaneHeights.size(); ++j) {
                    laneAccum += layout.takesLaneHeights[j];
                    if (y < laneAccum) {
                        outLayer = static_cast<uint32_t>(j);
                        return;
                    }
                }
            } else {
                outLayer = 0; // Main lane
            }
            return;
        }
        accum += layout.totalHeight;
    }
}

uint64_t PlaylistClipCanvas::xToFrame(double x) const
{
    if (m_view.zoomFactor <= 0.0) {
        return m_view.viewStartFrame;
    }
    const double frameD = m_view.viewStartFrame + x / m_view.zoomFactor;
    return (frameD >= 0.0) ? static_cast<uint64_t>(frameD) : 0;
}

double PlaylistClipCanvas::frameToX(uint64_t frame) const
{
    if (m_view.zoomFactor <= 0.0) {
        return 0.0;
    }
    const int64_t relFrame = static_cast<int64_t>(frame)
                           - static_cast<int64_t>(m_view.viewStartFrame);
    return static_cast<double>(relFrame) * m_view.zoomFactor;
}

double PlaylistClipCanvas::playheadX() const
{
    return frameToX(m_playheadFrame);
}


// ─────────────────────────────────────────────────────────────────────────────
// Hit-testing
// ─────────────────────────────────────────────────────────────────────────────

int PlaylistClipCanvas::hitTest(
    const QPointF& pos,
    const bridge::VisualRegion* regions,
    uint32_t count,
    CanvasDragState& outState,
    int* outPointIndex) const
{
    // First, check if the click hits any control point on an expanded, write-enabled sub-lane
    if (m_automation) {
        const int trackIndex = yToTrackIndex(pos.y());
        if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
            const auto& track = m_tracks[static_cast<size_t>(trackIndex)];
            const double trackY = getTrackYOffset(trackIndex) - static_cast<double>(m_view.verticalOffsetPx);
            const double relY = pos.y() - trackY;
            if (trackIndex < static_cast<int>(m_view.trackLayouts.size())) {
                const auto& layout = m_view.trackLayouts[static_cast<size_t>(trackIndex)];
                auto hit = layout.hitSubLaneAtY(relY);
                if (hit.index != -1 && m_automation->isAutomationWriteEnabled(track.trackId)) {
                    // Determine sub-lane geometry
                    double subLaneOffset = layout.getSubLaneOffsetForParam(hit.nodeId, hit.subNodeId, hit.paramIndex);
                    double subH = layout.subLanes[static_cast<size_t>(hit.index)].height;
                    QRectF subLaneRect(0.0, trackY + subLaneOffset, static_cast<double>(width()), subH);

                    const QRectF innerRect(
                        subLaneRect.left()   + 1.0,
                        subLaneRect.top()    + 1.0,
                        subLaneRect.width()  - 2.0,
                        subLaneRect.height() - 2.0
                    );

                    if (innerRect.height() > 4.0) {
                        const uint64_t startFrame = m_view.viewStartFrame;
                        const uint64_t endFrame = m_view.viewEndFrame;
                        const uint64_t durationFrames = (endFrame > startFrame) ? (endFrame - startFrame) : 1;

                        static constexpr uint32_t MAX_AUTO_PTS = 2048;
                        bridge::VisualAutomationPoint autoPts[MAX_AUTO_PTS];
                        uint32_t autoPtsCount = m_automation->getCurvePoints(
                            track.trackId,
                            hit.nodeId,
                            hit.subNodeId,
                            hit.paramIndex,
                            startFrame,
                            endFrame,
                            autoPts,
                            MAX_AUTO_PTS
                        );

                        const double scaleX = innerRect.width() / static_cast<double>(durationFrames);
                        const double scaleY = innerRect.height();

                        for (uint32_t ptIdx = 0; ptIdx < autoPtsCount; ++ptIdx) {
                            const int64_t relFrame = static_cast<int64_t>(autoPts[ptIdx].framePosition) - static_cast<int64_t>(startFrame);
                            const double ptX = innerRect.left() + static_cast<double>(relFrame) * scaleX;
                            const double ptY = innerRect.bottom() - static_cast<double>(autoPts[ptIdx].normalizedValue) * scaleY;

                            double dx = pos.x() - ptX;
                            double dy = pos.y() - ptY;
                            if (std::sqrt(dx*dx + dy*dy) <= 6.0) {
                                outState = CanvasDragState::DraggingControlPoint;
                                if (outPointIndex) {
                                    *outPointIndex = static_cast<int>(ptIdx);
                                }
                                m_automation->selectActiveAutomationLane(track.trackId, hit.nodeId, hit.subNodeId, static_cast<int32_t>(hit.paramIndex));
                                return -2 - trackIndex;
                            }
                        }
                    }
                }
            }
        }
    }

    // Priority order from plan §Phase 2-E lines 754-762:
    //  1. Fade-in handle zone
    //  2. Fade-out handle zone
    //  3. Resize-right zone
    //  4. Resize-left zone (SlipEdit)
    //  5. Gain badge rect
    //  6. Clip body → DraggingClip
    //  7. Empty lane → DrawingNewClip or SelectionRubberBand

    // Iterate in reverse so topmost (later-drawn) clip wins
    for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
        const QRectF clipRect = regionToRect(regions[i]);
        if (!clipRect.isValid() || !clipRect.contains(pos)) {
            continue;
        }

        const double px = pos.x();
        const double relRight = clipRect.right() - px;
        const double relLeft  = px - clipRect.left();

        // 1. Fade-in handle (left 8 px, top corner, if m_showFades is enabled)
        const double fadeZoneH = std::min(16.0, clipRect.height() * 0.5);
        if (regions[i].clipType == composition::RegionType::AUDIO 
            && m_showFades && (pos.y() - clipRect.top() <= fadeZoneH) && relLeft <= FADE_HANDLE_PX) {
            outState = CanvasDragState::DraggingFadeIn;
            return i;
        }

        // 2. Fade-out handle (right 8 px, top corner, if m_showFades is enabled)
        if (regions[i].clipType == composition::RegionType::AUDIO
            && m_showFades && (pos.y() - clipRect.top() <= fadeZoneH) && relRight <= FADE_HANDLE_PX) {
            outState = CanvasDragState::DraggingFadeOut;
            return i;
        }

        // 2a. Fade-in indicator line (anywhere vertically on the line, within 6px, if fade line is beyond corner handle)
        double fadeInPx = 0.0;
        if (regions[i].fadeInFrames > 0) {
            fadeInPx = static_cast<double>(regions[i].fadeInFrames) * m_view.zoomFactor;
        }
        if (regions[i].clipType == composition::RegionType::AUDIO 
            && m_showFades && fadeInPx > FADE_HANDLE_PX && std::abs(relLeft - fadeInPx) <= 6.0) {
            outState = CanvasDragState::DraggingFadeIn;
            return i;
        }

        // 2b. Fade-out indicator line (anywhere vertically on the line, within 6px, if fade line is beyond corner handle)
        double fadeOutPx = 0.0;
        if (regions[i].fadeOutFrames > 0) {
            fadeOutPx = static_cast<double>(regions[i].fadeOutFrames) * m_view.zoomFactor;
        }
        if (regions[i].clipType == composition::RegionType::AUDIO
            && m_showFades && fadeOutPx > FADE_HANDLE_PX && std::abs(relRight - fadeOutPx) <= 6.0) {
            outState = CanvasDragState::DraggingFadeOut;
            return i;
        }

        // 3. Resize-right (rightmost 6 px)
        if (relRight <= RESIZE_HANDLE_PX) {
            outState = CanvasDragState::ResizingClipRight;
            return i;
        }

        // 4. Resize-left / slip-edit (leftmost 6 px)
        if (relLeft <= RESIZE_HANDLE_PX) {
            outState = CanvasDragState::ResizingClipLeft;
            return i;
        }

        // 5. Gain badge (bottom-right corner)
        const QRectF gainBadge(
            clipRect.right() - GAIN_BADGE_W_PX - 3.0,
            clipRect.bottom() - GAIN_BADGE_H_PX - 3.0,
            GAIN_BADGE_W_PX,
            GAIN_BADGE_H_PX
        );
        if (regions[i].clipType == composition::RegionType::AUDIO
            && std::fabs(regions[i].gainLinear - 1.0f) > 0.001f
            && gainBadge.contains(pos))
        {
            outState = CanvasDragState::DraggingGainBadge;
            return i;
        }

        // 6. Clip body → Top Bar (Move) or Meat (CompHighlighting)
        const double TOP_BAR_HEIGHT = 15.0;
        if (pos.y() - clipRect.top() <= TOP_BAR_HEIGHT) {
            outState = CanvasDragState::DraggingClip;
        } else {
            outState = CanvasDragState::CompHighlighting;
        }
        return i;
    }

    // 7. Empty lane
    outState = CanvasDragState::Idle;
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
void PlaylistClipCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!m_arrangement) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    const QPointF pos = event->position();
    const int trackIndex = yToTrackIndex(pos.y());

    bridge::VisualRegion regions[MAX_VISIBLE];
    const uint32_t count = m_arrangement->getRegionsInViewport(
        m_view.viewStartFrame, m_view.viewEndFrame, regions, MAX_VISIBLE);

    if (m_automation) {
        if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
            const auto& track = m_tracks[static_cast<size_t>(trackIndex)];

            if (track.isAutomationExpanded && m_automation->isAutomationWriteEnabled(track.trackId) &&
                trackIndex < static_cast<int>(m_view.trackLayouts.size())) {
                const auto& layout = m_view.trackLayouts[static_cast<size_t>(trackIndex)];
                const double trackY    = getTrackYOffset(trackIndex)
                                       - static_cast<double>(m_view.verticalOffsetPx);
                const double relY      = pos.y() - trackY;

                auto hit = layout.hitSubLaneAtY(relY);
                if (hit.index != -1) {
                    m_automation->selectActiveAutomationLane(track.trackId, hit.nodeId, hit.subNodeId, static_cast<int32_t>(hit.paramIndex));

                    CanvasDragState hitState = CanvasDragState::Idle;
                    int ptIdx = -1;
                    hitTest(pos, regions, count, hitState, &ptIdx);

                    if (hitState == CanvasDragState::DraggingControlPoint && ptIdx >= 0) {
                        // Double-clicked an existing point: remove it
                        m_automation->removeAutomationPoint(static_cast<uint32_t>(ptIdx));
                    } else {
                        // Double-click on curve only: hit-test curve, insert at curve value
                        double subLaneOffset = layout.getSubLaneOffsetForParam(hit.nodeId, hit.subNodeId, hit.paramIndex);
                        double subH = layout.subLanes[static_cast<size_t>(hit.index)].height;
                        QRectF innerRect(0.0, trackY + subLaneOffset + 1.0,
                                         static_cast<double>(width()) - 2.0, subH - 2.0);

                        static constexpr uint32_t MAX_AUTO_PTS = 2048;
                        bridge::VisualAutomationPoint pts[MAX_AUTO_PTS];
                        uint32_t ptsCount = m_automation->getCurvePoints(
                            track.trackId, hit.nodeId, hit.subNodeId, hit.paramIndex,
                            m_view.viewStartFrame, m_view.viewEndFrame, pts, MAX_AUTO_PTS);

                        double defaultVal = 0.5;
                        if (m_automation) {
                            defaultVal = static_cast<double>(m_automation->getBaseParameterValue(hit.nodeId, hit.subNodeId, hit.paramIndex));
                        }

                        float curveVal = 0.0f;
                        if (AutomationClipItem::hitTestCurve(pos, innerRect, pts, ptsCount,
                                m_view.viewStartFrame,
                                m_view.viewEndFrame - m_view.viewStartFrame,
                                &curveVal,
                                defaultVal))
                        {
                            uint64_t frame = xToFrame(pos.x());
                            m_automation->addAutomationPoint(frame, curveVal);
                        }
                    }

                    update();
                    event->accept();
                    return;
                }
            }
        }
    }

    CanvasDragState hitState = CanvasDragState::Idle;
    int hitIdx = hitTest(pos, regions, count, hitState);

    // If focus mode caused the hit test to miss, do a direct bounding-box check
    // so users can still double-click clips to open their editors regardless of focus mode.
    if (hitIdx < 0) {
        for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
            QRectF clipRect = regionToRect(regions[i]);
            if (clipRect.contains(pos)) {
                hitIdx = i;
                break;
            }
        }
    }

    if (hitIdx >= 0) {
        const auto& r = regions[hitIdx];
        if (r.clipType == composition::RegionType::MIDI) {
            emit midiClipDoubleClicked(r.trackId, r.id);
            event->accept();
            return;
        } else if (r.clipType == composition::RegionType::AUDIO) {
            bool ok = false;
            QString newComment = DAWInputDialog::getMultiLineText(
                this,
                QStringLiteral("Edit Comment"),
                QStringLiteral("Edit Comment:"),
                QString::fromUtf8(r.comment),
                &ok
            );
            if (ok) {
                QString trimmed = newComment.left(static_cast<int>(MAX_COMMENT_LENGTH - 1));
                std::string commentStr = trimmed.toStdString();
                m_arrangement->updateRegionMetadata(r.id, r.name, commentStr.c_str(), r.colorARGB);
                update();
            }
            event->accept();
            return;
        }
    } else {
        if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
            const auto& track = m_tracks[static_cast<size_t>(trackIndex)];
            
            bool onSubLane = false;
            if (track.isAutomationExpanded && trackIndex < static_cast<int>(m_view.trackLayouts.size())) {
                const auto& layout = m_view.trackLayouts[static_cast<size_t>(trackIndex)];
                double trackY = getTrackYOffset(trackIndex) - static_cast<double>(m_view.verticalOffsetPx);
                if (layout.hitSubLaneAtY(pos.y() - trackY).index != -1) {
                    onSubLane = true;
                }
            }

            if (!onSubLane && (track.type == composition::TrackType::MIDI || track.type == composition::TrackType::INSTRUMENT)) {
                uint64_t insertFrame = snapFrame(xToFrame(pos.x()));
                uint64_t duration = 44100 * 4; // default fallback
                if (m_timeline) {
                    uint64_t tickStart = m_timeline->samplesToTicks(insertFrame);
                    uint64_t ticksPerBeat = m_timeline->getTicksPerBeat();
                    duration = m_timeline->ticksToSamples(tickStart + ticksPerBeat * 4) - insertFrame;
                }
                m_arrangement->insertMidiClip(track.trackId, insertFrame, duration);
                update();
                event->accept();
                return;
            }
        }
    }

    QWidget::mouseDoubleClickEvent(event);
}

void PlaylistClipCanvas::mousePressEvent(QMouseEvent* event)
{
    setFocus();

    if (event->button() == Qt::RightButton) {
        return;
    }

    if (!m_arrangement) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPointF pos = event->position();
    const int trackIndex = yToTrackIndex(pos.y());

    // Snapshot visible regions for hit-testing (stack allocated)
    bridge::VisualRegion regions[MAX_VISIBLE];
    const uint32_t count = m_arrangement->getRegionsInViewport(
        m_view.viewStartFrame, m_view.viewEndFrame, regions, MAX_VISIBLE);

    CanvasDragState hitState = CanvasDragState::Idle;
    int pointIdx = -1;
    const int hitIdx = hitTest(pos, regions, count, hitState, &pointIdx);

    m_dragAnchorPos    = pos;
    m_dragFrameDelta   = 0;
    m_dragGainDelta    = 0.0f;
    m_dragFadeDelta    = 0;
    m_rubberBandVisible = false;
    m_stretchMode       = false;

    bool isOnSubLane = false;
    TrackID activeTrackId = TrackID::invalid();
    NodeID activeNodeId = NodeID::invalid();
    uint32_t activeSubNodeId = 0;
    int32_t activeParamIndex = -1;

    if (m_automation) {
        if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
            const auto& track = m_tracks[static_cast<size_t>(trackIndex)];
            const double trackY = getTrackYOffset(trackIndex) - static_cast<double>(m_view.verticalOffsetPx);
            const double relY = pos.y() - trackY;
            if (trackIndex < static_cast<int>(m_view.trackLayouts.size())) {
                const auto& layout = m_view.trackLayouts[static_cast<size_t>(trackIndex)];
                auto hit = layout.hitSubLaneAtY(relY);
                if (hit.index != -1) {
                    isOnSubLane = true;
                    m_automation->selectActiveAutomationLane(track.trackId, hit.nodeId, hit.subNodeId, static_cast<int32_t>(hit.paramIndex));
                    activeTrackId = track.trackId;
                    activeNodeId = hit.nodeId;
                    activeSubNodeId = hit.subNodeId;
                    activeParamIndex = static_cast<int32_t>(hit.paramIndex);
                }
            }
        }
    }

    if (isOnSubLane && hitState == CanvasDragState::Idle &&
        ((event->modifiers() & Qt::AltModifier) || event->button() == Qt::MiddleButton)) {
        
        static constexpr uint32_t MAX_AUTO_PTS = 2048;
        bridge::VisualAutomationPoint autoPts[MAX_AUTO_PTS];
        uint32_t autoPtsCount = m_automation->getCurvePoints(
            activeTrackId, activeNodeId, activeSubNodeId, static_cast<uint32_t>(activeParamIndex),
            0, UINT64_MAX, autoPts, MAX_AUTO_PTS
        );

        uint64_t frameAtClick = xToFrame(pos.x());
        int leftPointIdx = -1;
        for (uint32_t i = 0; i < autoPtsCount; ++i) {
            if (autoPts[i].framePosition <= frameAtClick) {
                leftPointIdx = static_cast<int>(i);
            } else {
                break;
            }
        }

        if (leftPointIdx >= 0 && static_cast<uint32_t>(leftPointIdx) < autoPtsCount - 1) {
            if (autoPts[leftPointIdx + 1].framePosition > frameAtClick) {
                m_dragState = CanvasDragState::DraggingAutomationTension;
                m_dragActualPointIndex = autoPts[leftPointIdx].pointIndex;
                m_dragOrigTension = autoPts[leftPointIdx].tension;
                m_dragCurveShape = autoPts[leftPointIdx].curveShape;
                hitState = m_dragState;
            }
        }
    }

    if (hitState == CanvasDragState::DraggingControlPoint) {
        m_dragState = hitState;
        m_dragPointIndex = pointIdx;

        if (activeTrackId.isValid()) {
            static constexpr uint32_t MAX_AUTO_PTS = 2048;
            bridge::VisualAutomationPoint autoPts[MAX_AUTO_PTS];
            uint32_t autoPtsCount = m_automation->getCurvePoints(
                activeTrackId,
                activeNodeId,
                activeSubNodeId,
                static_cast<uint32_t>(activeParamIndex),
                m_view.viewStartFrame,
                m_view.viewEndFrame,
                autoPts,
                MAX_AUTO_PTS
            );
            if (m_dragPointIndex >= 0 && m_dragPointIndex < static_cast<int>(autoPtsCount)) {
                m_dragOrigPointFrame = autoPts[m_dragPointIndex].framePosition;
                m_dragOrigPointValue = autoPts[m_dragPointIndex].normalizedValue;
            }
        }
    } else if (hitIdx >= 0) {
        // Handle selection state on clip click
        uint64_t regionRaw = regions[hitIdx].id.toRaw();
        if ((event->modifiers() & Qt::ShiftModifier) && m_lastSelectedRegionId.isValid()) {
            bridge::VisualRegion lastRegion;
            if (m_arrangement->getVisualRegion(m_lastSelectedRegionId, lastRegion)) {
                // Find track indices
                int lastTrackIdx = -1;
                int currentTrackIdx = -1;
                for (size_t i = 0; i < m_tracks.size(); ++i) {
                    if (m_tracks[i].trackId == lastRegion.trackId) {
                        lastTrackIdx = static_cast<int>(i);
                    }
                    if (m_tracks[i].trackId == regions[hitIdx].trackId) {
                        currentTrackIdx = static_cast<int>(i);
                    }
                }
                if (lastTrackIdx != -1 && currentTrackIdx != -1) {
                    int minTrackIdx = std::min(lastTrackIdx, currentTrackIdx);
                    int maxTrackIdx = std::max(lastTrackIdx, currentTrackIdx);
                    
                    uint64_t minStart = std::min(lastRegion.startFrame, regions[hitIdx].startFrame);
                    uint64_t maxEnd = std::max(lastRegion.startFrame + lastRegion.durationFrames, 
                                               regions[hitIdx].startFrame + regions[hitIdx].durationFrames);
                    
                    // Fetch all regions in this frame range
                    static constexpr uint32_t MAX_RANGE_SELECT = 1024;
                    std::vector<bridge::VisualRegion> rangeRegions(MAX_RANGE_SELECT);
                    uint32_t rangeCount = m_arrangement->getRegionsInViewport(minStart, maxEnd, rangeRegions.data(), MAX_RANGE_SELECT);
                    rangeCount = std::min(rangeCount, MAX_RANGE_SELECT);
                    
                    m_selectedRegions.clear();
                    for (uint32_t i = 0; i < rangeCount; ++i) {
                        const auto& r = rangeRegions[i];
                        // Find the track index for this region
                        int rTrackIdx = -1;
                        for (size_t t = 0; t < m_tracks.size(); ++t) {
                            if (m_tracks[t].trackId == r.trackId) {
                                rTrackIdx = static_cast<int>(t);
                                break;
                            }
                        }
                        if (rTrackIdx >= minTrackIdx && rTrackIdx <= maxTrackIdx) {
                            m_selectedRegions.insert(r.id.toRaw());
                        }
                    }
                }
            } else {
                m_selectedRegions.clear();
                m_selectedRegions.insert(regionRaw);
                m_lastSelectedRegionId = regions[hitIdx].id;
            }
        } else if (event->modifiers() & Qt::ControlModifier) {
            if (m_selectedRegions.find(regionRaw) != m_selectedRegions.end()) {
                m_selectedRegions.erase(regionRaw);
                if (m_lastSelectedRegionId == regions[hitIdx].id) {
                    m_lastSelectedRegionId = bridge::RegionID::invalid();
                }
            } else {
                m_selectedRegions.insert(regionRaw);
                m_lastSelectedRegionId = regions[hitIdx].id;
            }
        } else {
            if (m_selectedRegions.find(regionRaw) == m_selectedRegions.end()) {
                m_selectedRegions.clear();
                m_selectedRegions.insert(regionRaw);
            }
            m_lastSelectedRegionId = regions[hitIdx].id;
        }

        m_dragState    = hitState;
        if (m_dragState == CanvasDragState::DraggingClip) {
            setCursor(Qt::ClosedHandCursor);
        } else if (m_dragState == CanvasDragState::ResizingClipLeft || m_dragState == CanvasDragState::ResizingClipRight) {
            setCursor(Qt::SizeHorCursor);
        } else if (m_dragState == CanvasDragState::DraggingFadeIn || m_dragState == CanvasDragState::DraggingFadeOut) {
            setCursor(Qt::PointingHandCursor);
        }
        m_dragRegionId = regions[hitIdx].id;
        m_dragOrigTrackId = regions[hitIdx].trackId;
        m_dragOrigTrackIndex = trackIndex;
        m_dragOrigLayerIndex = regions[hitIdx].layerIndex;
        m_dragStartFrame = xToFrame(pos.x());
        
        if (hitState == CanvasDragState::CompHighlighting) {
            m_compHighlightStartFrame = snapFrame(m_dragStartFrame);
            m_compHighlightEndFrame = m_compHighlightStartFrame;
            m_compHighlightLane = static_cast<int>(regions[hitIdx].layerIndex);
            m_compHighlightTrack = regions[hitIdx].trackId;
            m_compHighlightClipType = regions[hitIdx].clipType;
            if (regions[hitIdx].clipType == composition::RegionType::AUTOMATION) {
                m_compHighlightNodeId = regions[hitIdx].automationTargetNodeId;
                m_compHighlightParamIndex = regions[hitIdx].automationParameterIndex;
            } else {
                m_compHighlightNodeId = NodeID::invalid();
                m_compHighlightParamIndex = 0;
            }
        }
        m_dragOrigStartFrame = regions[hitIdx].startFrame;
        m_dragOrigSourceStart = regions[hitIdx].fileOffsetFrames;
        m_dragOrigDuration = regions[hitIdx].durationFrames;
        m_dragOrigSourceLength = regions[hitIdx].sourceLengthFrames;
        m_dragOrigRatio = static_cast<double>(regions[hitIdx].playbackRatio);
        m_dragDestTrackIndex = yToTrackIndex(pos.y());
        m_stretchMode = (event->modifiers() & Qt::ShiftModifier) && 
                        (hitState == CanvasDragState::ResizingClipRight || hitState == CanvasDragState::ResizingClipLeft);

        if (hitState == CanvasDragState::DraggingGainBadge) {
            m_dragOrigGain = regions[hitIdx].gainLinear;
        } else if (hitState == CanvasDragState::DraggingFadeIn) {
            m_dragOrigFadeIn  = regions[hitIdx].fadeInFrames;
            m_dragOrigFadeOut = regions[hitIdx].fadeOutFrames;
        } else if (hitState == CanvasDragState::DraggingFadeOut) {
            m_dragOrigFadeIn  = regions[hitIdx].fadeInFrames;
            m_dragOrigFadeOut = regions[hitIdx].fadeOutFrames;
        }
    } else if (m_dragState != CanvasDragState::DraggingAutomationTension) {
        if (isOnSubLane) {
            m_dragState = CanvasDragState::CompHighlighting;
            m_dragStartFrame = xToFrame(pos.x());
            m_compHighlightStartFrame = snapFrame(m_dragStartFrame);
            m_compHighlightEndFrame = m_compHighlightStartFrame;
            m_compHighlightLane = 0; // satisfies lane >= 0
            m_compHighlightTrack = activeTrackId;
            m_compHighlightClipType = composition::RegionType::AUTOMATION;
            m_compHighlightNodeId = activeNodeId;
            m_compHighlightParamIndex = static_cast<uint32_t>(activeParamIndex);
        } else {
            if (!(event->modifiers() & Qt::ControlModifier)) {
                m_selectedRegions.clear();
            }
            m_initialSelectedRegions = m_selectedRegions;
            // Empty lane — start rubber-band or new clip
            m_dragState = CanvasDragState::SelectionRubberBand;
            m_rubberBandRect    = QRectF(pos, pos);
            m_rubberBandVisible = true;
        }
    }

    update();
    event->accept();
}

void PlaylistClipCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragState == CanvasDragState::Idle) {
        // --- Hover Cursor Support ---
        if (m_arrangement) {
            bridge::VisualRegion regions[MAX_VISIBLE];
            const uint32_t count = m_arrangement->getRegionsInViewport(
                m_view.viewStartFrame, m_view.viewEndFrame, regions, MAX_VISIBLE);
            
            CanvasDragState hitState = CanvasDragState::Idle;
            int pointIdx = -1;
            hitTest(event->position(), regions, count, hitState, &pointIdx);
            
            if (hitState == CanvasDragState::CompHighlighting) {
                setCursor(Qt::IBeamCursor);
            } else if (hitState == CanvasDragState::ResizingClipLeft || hitState == CanvasDragState::ResizingClipRight) {
                setCursor(Qt::SizeHorCursor);
            } else if (hitState == CanvasDragState::DraggingFadeIn || hitState == CanvasDragState::DraggingFadeOut) {
                setCursor(Qt::PointingHandCursor);
            } else if (hitState == CanvasDragState::DraggingControlPoint) {
                setCursor(Qt::PointingHandCursor);
            } else if (hitState == CanvasDragState::Idle) {
                // Check if hovering over an automation sub-lane (no control point hit)
                const int trackIndex = yToTrackIndex(event->position().y());
                if (trackIndex >= 0 && trackIndex < static_cast<int>(m_view.trackLayouts.size())) {
                    const auto& layout = m_view.trackLayouts[static_cast<size_t>(trackIndex)];
                    const double trackY = getTrackYOffset(trackIndex) - static_cast<double>(m_view.verticalOffsetPx);
                    auto hit = layout.hitSubLaneAtY(event->position().y() - trackY);
                    if (hit.index != -1) {
                        bool overCurve = false;
                        if (m_automation && trackIndex < static_cast<int>(m_tracks.size())) {
                            const auto& track = m_tracks[static_cast<size_t>(trackIndex)];
                            if (m_automation->isAutomationWriteEnabled(track.trackId)) {
                                double subLaneOffset = layout.getSubLaneOffsetForParam(hit.nodeId, hit.subNodeId, hit.paramIndex);
                                double subH = layout.subLanes[static_cast<size_t>(hit.index)].height;
                                QRectF innerRect(0.0, trackY + subLaneOffset + 1.0,
                                                 static_cast<double>(width()) - 2.0, subH - 2.0);

                                static constexpr uint32_t MAX_AUTO_PTS = 2048;
                                bridge::VisualAutomationPoint pts[MAX_AUTO_PTS];
                                uint32_t ptsCount = m_automation->getCurvePoints(
                                    track.trackId, hit.nodeId, hit.subNodeId, hit.paramIndex,
                                    m_view.viewStartFrame, m_view.viewEndFrame, pts, MAX_AUTO_PTS);

                                double defaultVal = 0.5;
                                if (m_automation) {
                                    defaultVal = static_cast<double>(m_automation->getBaseParameterValue(hit.nodeId, hit.subNodeId, hit.paramIndex));
                                }

                                float curveVal = 0.0f;
                                if (AutomationClipItem::hitTestCurve(event->position(), innerRect, pts, ptsCount,
                                        m_view.viewStartFrame,
                                        m_view.viewEndFrame - m_view.viewStartFrame,
                                        &curveVal,
                                        defaultVal)) {
                                    overCurve = true;
                                }
                            }
                        }

                        if (overCurve) {
                            setCursor(Qt::PointingHandCursor);
                        } else {
                            setCursor(Qt::IBeamCursor);
                        }
                    } else {
                        setCursor(Qt::ArrowCursor);
                    }
                } else {
                    setCursor(Qt::ArrowCursor);
                }
            } else {
                setCursor(Qt::ArrowCursor);
            }
        }
        
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPointF pos    = event->position();
    const double  dxPx   = pos.x() - m_dragAnchorPos.x();
    const double  dyPx   = pos.y() - m_dragAnchorPos.y();

    switch (m_dragState) {
        case CanvasDragState::CompHighlighting: {
            uint64_t currentFrame = xToFrame(pos.x());
            int dir = (currentFrame >= m_compHighlightStartFrame) ? 1 : -1;
            
            // Do not recalculate m_compHighlightStartFrame to prevent the anchor from jumping
            // when the user changes drag direction.
            m_compHighlightEndFrame = snapFrame(currentFrame, dir);
            update();
            break;
        }
        case CanvasDragState::DraggingClip: {
            // Convert pixel delta to frame delta
            if (m_view.zoomFactor > 0.0) {
                m_dragFrameDelta = static_cast<int64_t>(dxPx / m_view.zoomFactor);
            }
            yToTrackAndLayer(pos.y(), m_dragDestTrackIndex, m_dragDestLayerIndex);
            break;
        }

        case CanvasDragState::ResizingClipRight: {
            if (m_view.zoomFactor > 0.0) {
                m_dragFrameDelta = static_cast<int64_t>(dxPx / m_view.zoomFactor);
                bridge::VisualRegion dragRegion;
                if (m_dragRegionId.isValid() && m_arrangement->getVisualRegion(m_dragRegionId, dragRegion)) {
                    int64_t maxDelta;
                    if (dragRegion.sourceLengthFrames == ~0ULL) {
                        maxDelta = std::numeric_limits<int64_t>::max() - 1 - static_cast<int64_t>(dragRegion.durationFrames);
                    } else {
                        maxDelta = static_cast<int64_t>(dragRegion.sourceLengthFrames - dragRegion.fileOffsetFrames - dragRegion.durationFrames);
                    }
                    int64_t minDelta = 1 - static_cast<int64_t>(dragRegion.durationFrames);
                    m_dragFrameDelta = std::clamp(m_dragFrameDelta, minDelta, maxDelta);
                }
            }
            break;
        }

        case CanvasDragState::ResizingClipLeft: {
            if (m_view.zoomFactor > 0.0) {
                m_dragFrameDelta = static_cast<int64_t>(dxPx / m_view.zoomFactor);
                bridge::VisualRegion dragRegion;
                if (m_dragRegionId.isValid() && m_arrangement->getVisualRegion(m_dragRegionId, dragRegion)) {
                    int64_t minDelta = -static_cast<int64_t>(dragRegion.fileOffsetFrames);
                    int64_t maxDelta = static_cast<int64_t>(dragRegion.durationFrames) - 1;
                    m_dragFrameDelta = std::clamp(m_dragFrameDelta, minDelta, maxDelta);
                }
            }
            break;
        }

        case CanvasDragState::DraggingFadeIn: {
            if (m_view.zoomFactor > 0.0) {
                const int64_t delta = static_cast<int64_t>(dxPx / m_view.zoomFactor);
                m_dragFadeDelta = delta;
            }
            break;
        }

        case CanvasDragState::DraggingFadeOut: {
            if (m_view.zoomFactor > 0.0) {
                const int64_t delta = static_cast<int64_t>(-dxPx / m_view.zoomFactor);
                m_dragFadeDelta = delta;
            }
            break;
        }

        case CanvasDragState::DraggingGainBadge: {
            // Vertical drag: -1 px = +0.01 linear gain (up = louder)
            m_dragGainDelta = static_cast<float>(-dyPx * 0.01);
            break;
        }

        case CanvasDragState::SelectionRubberBand:
        case CanvasDragState::DrawingNewClip: {
            m_rubberBandRect = QRectF(m_dragAnchorPos, pos).normalized();
            if (m_dragState == CanvasDragState::SelectionRubberBand) {
                m_selectedRegions = m_initialSelectedRegions;
                if (m_arrangement && m_view.viewEndFrame > m_view.viewStartFrame) {
                    bridge::VisualRegion regions[MAX_VISIBLE];
                    const uint32_t count = m_arrangement->getRegionsInViewport(
                        m_view.viewStartFrame, m_view.viewEndFrame, regions, MAX_VISIBLE);
                    
                    for (uint32_t i = 0; i < count; ++i) {
                        const QRectF clipRect = regionToRect(regions[i]);
                        if (m_rubberBandRect.intersects(clipRect)) {
                            const uint64_t rawId = regions[i].id.toRaw();
                            if (event->modifiers() & Qt::ControlModifier) {
                                if (m_initialSelectedRegions.find(rawId) != m_initialSelectedRegions.end()) {
                                    m_selectedRegions.erase(rawId);
                                } else {
                                    m_selectedRegions.insert(rawId);
                                }
                            } else {
                                m_selectedRegions.insert(rawId);
                            }
                        }
                    }
                }
            }
            break;
        }

        case CanvasDragState::DraggingControlPoint: {
            if (m_automation && m_dragPointIndex >= 0) {
                TrackID activeTrackId = TrackID::invalid();
                NodeID activeNodeId = NodeID::invalid();
                uint32_t activeSubNodeId = 0;
                int32_t activeParamIndex = -1;
                m_automation->getActiveAutomationLane(activeTrackId, activeNodeId, activeSubNodeId, activeParamIndex);

                if (activeTrackId.isValid()) {
                    int trackRow = -1;
                    for (int t = 0; t < static_cast<int>(m_tracks.size()); ++t) {
                        if (m_tracks[static_cast<size_t>(t)].trackId == activeTrackId) {
                            trackRow = t;
                            break;
                        }
                    }

                    if (trackRow >= 0 && trackRow < static_cast<int>(m_view.trackLayouts.size())) {
                        const auto& layout = m_view.trackLayouts[static_cast<size_t>(trackRow)];
                        double trackY = getTrackYOffset(trackRow) - static_cast<double>(m_view.verticalOffsetPx);
                        double offset = layout.getSubLaneOffsetForParam(activeNodeId, activeSubNodeId, static_cast<uint32_t>(activeParamIndex));
                        double subH = layout.getSubLaneHeightForParam(activeNodeId, activeSubNodeId, static_cast<uint32_t>(activeParamIndex));

                        double subLaneY = trackY + offset;
                        double innerTop = subLaneY + 1.0;
                        double innerBottom = subLaneY + subH - 1.0;
                        double innerHeight = innerBottom - innerTop;

                        float targetValue = 0.0f;
                        if (innerHeight > 0.0) {
                            targetValue = static_cast<float>((innerBottom - pos.y()) / innerHeight);
                        }
                        targetValue = std::clamp(targetValue, 0.0f, 1.0f);

                        uint64_t targetFrame = xToFrame(pos.x());

                        int64_t frameDelta = static_cast<int64_t>(targetFrame) - static_cast<int64_t>(m_dragOrigPointFrame);
                        float valueDelta = targetValue - m_dragOrigPointValue;

                        uint32_t ptIdx = static_cast<uint32_t>(m_dragPointIndex);
                        m_automation->editPoints(&ptIdx, 1, frameDelta, valueDelta);

                        static constexpr uint32_t MAX_AUTO_PTS = 2048;
                        bridge::VisualAutomationPoint autoPts[MAX_AUTO_PTS];
                        uint32_t newCount = m_automation->getCurvePoints(
                            activeTrackId,
                            activeNodeId,
                            activeSubNodeId,
                            static_cast<uint32_t>(activeParamIndex),
                            m_view.viewStartFrame,
                            m_view.viewEndFrame,
                            autoPts,
                            MAX_AUTO_PTS
                        );

                        int newIdx = -1;
                        uint64_t minDiff = std::numeric_limits<uint64_t>::max();
                        for (uint32_t pt = 0; pt < newCount; ++pt) {
                            uint64_t diff = (autoPts[pt].framePosition > targetFrame)
                                ? autoPts[pt].framePosition - targetFrame
                                : targetFrame - autoPts[pt].framePosition;
                            if (diff < minDiff) {
                                minDiff = diff;
                                newIdx = static_cast<int>(pt);
                            }
                        }

                        if (newIdx != -1) {
                            m_dragPointIndex = newIdx;
                            m_dragOrigPointFrame = autoPts[newIdx].framePosition;
                            m_dragOrigPointValue = autoPts[newIdx].normalizedValue;

                            // Populate HUD tooltip with formatted parameter value
                            m_hudVisible = true;
                            m_hudPos     = pos + QPointF(12.0, -22.0);
                            m_hudText    = [&]() -> QString {
                                const float v = m_dragOrigPointValue;
                                std::string semanticName = m_automation->queryPluginParameterName(activeTrackId, activeNodeId, activeSubNodeId, static_cast<uint32_t>(activeParamIndex));
                                
                                // Pan formatting
                                if (semanticName.find("Pan") != std::string::npos) {
                                    if (std::fabs(v - 0.5f) < 1e-4f) return QStringLiteral("Center");
                                    const double pct = std::fabs(static_cast<double>(v) - 0.5) * 200.0;
                                    return (v < 0.5f)
                                        ? QString::asprintf("L %.0f%%", pct)
                                        : QString::asprintf("R %.0f%%", pct);
                                }
                                
                                // Volume/Level/Gain formatting (dB)
                                if (semanticName.find("Volume") != std::string::npos ||
                                    semanticName.find("Level") != std::string::npos ||
                                    semanticName.find("Gain") != std::string::npos) {
                                    float gainLinear = Math::Gain::normalizedToLinear(v);
                                    if (gainLinear <= 0.000001f) return QStringLiteral("\u2212\u221e dB");
                                    float db = Math::Gain::coeffTodB(gainLinear);
                                    if (std::fabs(db) < 0.05f) return QStringLiteral("0.0 dB");
                                    return QString::asprintf("%+.1f dB", static_cast<double>(db));
                                }
                                
                                // Default fallback
                                return QString::asprintf("%.3f", static_cast<double>(v));
                            }();
                        }
                    }
                }
            }
            break;
        }

        case CanvasDragState::DraggingAutomationTension: {
            if (m_automation) {
                double dy = m_dragAnchorPos.y() - pos.y(); // Positive if dragging UP
                float tensionDelta = static_cast<float>(dy / 100.0);
                float newTension = m_dragOrigTension + tensionDelta;
                newTension = std::max(-1.0f, std::min(1.0f, newTension));
                m_automation->setPointShapeAndTension(m_dragActualPointIndex, m_dragCurveShape, newTension);
                
                m_hudVisible = true;
                m_hudPos = pos + QPointF(12.0, -22.0);
                m_hudText = QString::asprintf("Tension: %+.2f", static_cast<double>(newTension));
            }
            break;
        }

        default:
            break;
    }

    // Live preview: repaint each move event (no bridge calls!)
    update();
    event->accept();
}

void PlaylistClipCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    // Commit mutations ONLY on release (plan §Phase 2-E rule)
    switch (m_dragState) {
        case CanvasDragState::CompHighlighting: {
            if (m_compHighlightStartFrame > m_compHighlightEndFrame) {
                std::swap(m_compHighlightStartFrame, m_compHighlightEndFrame);
            }
            break;
        }
        case CanvasDragState::DraggingClip: {
            bool hasMoved = (m_dragFrameDelta != 0) || 
                            (m_dragDestLayerIndex != 0xFFFFFFFF && m_dragDestLayerIndex != m_dragOrigLayerIndex) ||
                            (m_dragDestTrackIndex != -1 && m_dragDestTrackIndex != m_dragOrigTrackIndex);
            if (hasMoved) {
                if (m_selectedRegions.find(m_dragRegionId.toRaw()) != m_selectedRegions.end()) {
                    for (uint64_t rawId : m_selectedRegions) {
                        bridge::VisualRegion region;
                        if (m_arrangement->getVisualRegion(bridge::RegionID::fromRaw(rawId), region)) {
                            TrackID destTrack = region.trackId;
                            if (m_dragDestTrackIndex >= 0 && m_dragDestTrackIndex < static_cast<int>(m_tracks.size()) &&
                                m_dragOrigTrackIndex >= 0 && m_dragOrigTrackIndex < static_cast<int>(m_tracks.size()) && !m_dragHoveringEmptySpace) {
                                
                                int rTrackIdx = -1;
                                for (size_t t = 0; t < m_tracks.size(); ++t) {
                                    if (m_tracks[t].trackId == region.trackId) {
                                        rTrackIdx = static_cast<int>(t);
                                        break;
                                    }
                                }
                                if (rTrackIdx != -1) {
                                    int newTrackIdx = std::clamp(rTrackIdx + (m_dragDestTrackIndex - m_dragOrigTrackIndex), 0, static_cast<int>(m_tracks.size()) - 1);
                                    destTrack = m_tracks[static_cast<size_t>(newTrackIdx)].trackId;
                                }
                            }
                            
                            const int64_t rawNewStart = static_cast<int64_t>(region.startFrame) + m_dragFrameDelta;
                            const int64_t snappedStart = (rawNewStart < 0) ? 0 : static_cast<int64_t>(snapFrame(static_cast<uint64_t>(rawNewStart)));
                            
                            uint32_t destLayer = region.layerIndex;
                            if (m_dragDestLayerIndex != 0xFFFFFFFF && m_dragOrigLayerIndex != 0xFFFFFFFF) {
                                int newLayer = static_cast<int>(region.layerIndex) + (static_cast<int>(m_dragDestLayerIndex) - static_cast<int>(m_dragOrigLayerIndex));
                                destLayer = static_cast<uint32_t>(std::max(0, newLayer));
                            }
                            
                            emit regionMoveRequested(region.id, destTrack, snappedStart, destLayer);
                        }
                    }
                } else {
                    TrackID destTrack = TrackID::invalid();
                    if (m_dragDestTrackIndex >= 0
                        && m_dragDestTrackIndex < static_cast<int>(m_tracks.size()))
                    {
                        destTrack = m_tracks[static_cast<size_t>(m_dragDestTrackIndex)].trackId;
                    }
                    const int64_t rawNewStart = static_cast<int64_t>(m_dragOrigStartFrame) + m_dragFrameDelta;
                    const int64_t snappedStart = (rawNewStart < 0) ? 0 : static_cast<int64_t>(snapFrame(static_cast<uint64_t>(rawNewStart)));
                    emit regionMoveRequested(m_dragRegionId, destTrack, snappedStart, m_dragDestLayerIndex);
                }
            } else {
                if (!(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
                    m_selectedRegions.clear();
                    m_selectedRegions.insert(m_dragRegionId.toRaw());
                }
            }
            break;
        }

        case CanvasDragState::ResizingClipRight: {
            if (m_selectedRegions.find(m_dragRegionId.toRaw()) != m_selectedRegions.end()) {
                for (uint64_t rawId : m_selectedRegions) {
                    bridge::VisualRegion region;
                    if (m_arrangement->getVisualRegion(bridge::RegionID::fromRaw(rawId), region)) {
                        if (region.durationFrames > 0) {
                            if (m_stretchMode) {
                                const int64_t rawEnd = static_cast<int64_t>(region.startFrame + region.durationFrames) + m_dragFrameDelta;
                                const uint64_t snappedEnd = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawEnd)));
                                if (snappedEnd > region.startFrame) {
                                    const uint64_t snappedDuration = snappedEnd - region.startFrame;
                                    const double ratio = static_cast<double>(region.playbackRatio) * (static_cast<double>(region.durationFrames) / static_cast<double>(snappedDuration));
                                    emit regionTrimRequested(region.id, region.startFrame, region.fileOffsetFrames, snappedDuration);
                                    emit regionStretchRequested(region.id, ratio);
                                }
                            } else {
                                const int64_t rawEnd = static_cast<int64_t>(region.startFrame + region.durationFrames) + m_dragFrameDelta;
                                int64_t maxEnd = (region.sourceLengthFrames == ~0ULL)
                                    ? std::numeric_limits<int64_t>::max() - 1
                                    : static_cast<int64_t>(region.startFrame) + static_cast<int64_t>(region.sourceLengthFrames - region.fileOffsetFrames);
                                int64_t rawEndClamped = std::min(rawEnd, maxEnd);
                                const uint64_t snappedEnd = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawEndClamped)));
                                if (snappedEnd > region.startFrame) {
                                    const uint64_t snappedDuration = snappedEnd - region.startFrame;
                                    emit regionTrimRequested(region.id, region.startFrame, region.fileOffsetFrames, snappedDuration);
                                }
                            }
                        }
                    }
                }
            } else {
                if (m_stretchMode) {
                    if (m_dragOrigDuration > 0) {
                        const int64_t rawEnd = static_cast<int64_t>(m_dragOrigStartFrame + m_dragOrigDuration) + m_dragFrameDelta;
                        const uint64_t snappedEnd = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawEnd)));
                        if (snappedEnd > m_dragOrigStartFrame) {
                            const uint64_t snappedDuration = snappedEnd - m_dragOrigStartFrame;
                            const double ratio = m_dragOrigRatio * (static_cast<double>(m_dragOrigDuration) / static_cast<double>(snappedDuration));
                            emit regionTrimRequested(m_dragRegionId, m_dragOrigStartFrame, m_dragOrigSourceStart, snappedDuration);
                            emit regionStretchRequested(m_dragRegionId, ratio);
                        }
                    }
                } else {
                    if (m_dragOrigDuration > 0) {
                        const int64_t rawEnd = static_cast<int64_t>(m_dragOrigStartFrame + m_dragOrigDuration) + m_dragFrameDelta;
                        int64_t maxEnd = (m_dragOrigSourceLength == ~0ULL)
                            ? std::numeric_limits<int64_t>::max() - 1
                            : static_cast<int64_t>(m_dragOrigStartFrame) + static_cast<int64_t>(m_dragOrigSourceLength - m_dragOrigSourceStart);
                        int64_t rawEndClamped = std::min(rawEnd, maxEnd);
                        const uint64_t snappedEnd = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawEndClamped)));
                        if (snappedEnd > m_dragOrigStartFrame) {
                            const uint64_t snappedDuration = snappedEnd - m_dragOrigStartFrame;
                            emit regionTrimRequested(m_dragRegionId, m_dragOrigStartFrame, m_dragOrigSourceStart, snappedDuration);
                        }
                    }
                }
            }
            break;
        }

        case CanvasDragState::ResizingClipLeft: {
            if (m_selectedRegions.find(m_dragRegionId.toRaw()) != m_selectedRegions.end()) {
                for (uint64_t rawId : m_selectedRegions) {
                    bridge::VisualRegion region;
                    if (m_arrangement->getVisualRegion(bridge::RegionID::fromRaw(rawId), region)) {
                        if (region.durationFrames > 0) {
                            if (m_stretchMode) {
                                const int64_t rawStart = static_cast<int64_t>(region.startFrame) + m_dragFrameDelta;
                                const uint64_t snappedStart = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawStart)));
                                const uint64_t origEnd = region.startFrame + region.durationFrames;
                                if (snappedStart < origEnd) {
                                    const uint64_t snappedDuration = origEnd - snappedStart;
                                    const double ratio = static_cast<double>(region.playbackRatio) * (static_cast<double>(region.durationFrames) / static_cast<double>(snappedDuration));
                                    emit regionTrimRequested(region.id, snappedStart, region.fileOffsetFrames, snappedDuration);
                                    emit regionStretchRequested(region.id, ratio);
                                }
                            } else {
                                const int64_t rawStart = static_cast<int64_t>(region.startFrame) + m_dragFrameDelta;
                                int64_t minStart = static_cast<int64_t>(region.startFrame) - static_cast<int64_t>(region.fileOffsetFrames);
                                int64_t maxStart = static_cast<int64_t>(region.startFrame + region.durationFrames) - 1;
                                int64_t rawStartClamped = std::clamp(rawStart, std::max(int64_t{0}, minStart), maxStart);
                                const uint64_t snappedStart = snapFrame(static_cast<uint64_t>(rawStartClamped));
                                const uint64_t origEnd = region.startFrame + region.durationFrames;
                                if (snappedStart < origEnd) {
                                    const uint64_t snappedDuration = origEnd - snappedStart;
                                    const uint64_t newSourceStart = region.fileOffsetFrames + (snappedStart - region.startFrame);
                                    emit regionTrimRequested(region.id, snappedStart, newSourceStart, snappedDuration);
                                }
                            }
                        }
                    }
                }
            } else {
                if (m_stretchMode) {
                    if (m_dragOrigDuration > 0) {
                        const int64_t rawStart = static_cast<int64_t>(m_dragOrigStartFrame) + m_dragFrameDelta;
                        const uint64_t snappedStart = snapFrame(static_cast<uint64_t>(std::max(int64_t{0}, rawStart)));
                        const uint64_t origEnd = m_dragOrigStartFrame + m_dragOrigDuration;
                        if (snappedStart < origEnd) {
                            const uint64_t snappedDuration = origEnd - snappedStart;
                            const double ratio = m_dragOrigRatio * (static_cast<double>(m_dragOrigDuration) / static_cast<double>(snappedDuration));
                            emit regionTrimRequested(m_dragRegionId, snappedStart, m_dragOrigSourceStart, snappedDuration);
                            emit regionStretchRequested(m_dragRegionId, ratio);
                        }
                    }
                } else {
                    if (m_dragOrigDuration > 0) {
                        const int64_t rawStart = static_cast<int64_t>(m_dragOrigStartFrame) + m_dragFrameDelta;
                        int64_t minStart = static_cast<int64_t>(m_dragOrigStartFrame) - static_cast<int64_t>(m_dragOrigSourceStart);
                        int64_t maxStart = static_cast<int64_t>(m_dragOrigStartFrame + m_dragOrigDuration) - 1;
                        int64_t rawStartClamped = std::clamp(rawStart, std::max(int64_t{0}, minStart), maxStart);
                        const uint64_t snappedStart = snapFrame(static_cast<uint64_t>(rawStartClamped));
                        const uint64_t origEnd = m_dragOrigStartFrame + m_dragOrigDuration;
                        if (snappedStart < origEnd) {
                            const uint64_t snappedDuration = origEnd - snappedStart;
                            const uint64_t newSourceStart = m_dragOrigSourceStart + (snappedStart - m_dragOrigStartFrame);
                            emit regionTrimRequested(m_dragRegionId, snappedStart, newSourceStart, snappedDuration);
                        }
                    }
                }
            }
            break;
        }

        case CanvasDragState::DraggingFadeIn: {
            if (m_selectedRegions.find(m_dragRegionId.toRaw()) != m_selectedRegions.end()) {
                for (uint64_t rawId : m_selectedRegions) {
                    bridge::VisualRegion region;
                    if (m_arrangement->getVisualRegion(bridge::RegionID::fromRaw(rawId), region)) {
                        const int64_t newFadeIn = static_cast<int64_t>(region.fadeInFrames) + m_dragFadeDelta;
                        const auto clampedFadeIn = static_cast<uint32_t>(std::clamp(newFadeIn, int64_t{0}, static_cast<int64_t>(region.durationFrames)));
                        emit regionFadesChanged(region.id, clampedFadeIn, region.fadeOutFrames);
                    }
                }
            } else {
                const int64_t newFadeIn = static_cast<int64_t>(m_dragOrigFadeIn) + m_dragFadeDelta;
                const auto clampedFadeIn = static_cast<uint32_t>(std::max(int64_t{0}, newFadeIn));
                emit regionFadesChanged(m_dragRegionId, clampedFadeIn, m_dragOrigFadeOut);
            }
            break;
        }

        case CanvasDragState::DraggingFadeOut: {
            if (m_selectedRegions.find(m_dragRegionId.toRaw()) != m_selectedRegions.end()) {
                for (uint64_t rawId : m_selectedRegions) {
                    bridge::VisualRegion region;
                    if (m_arrangement->getVisualRegion(bridge::RegionID::fromRaw(rawId), region)) {
                        const int64_t newFadeOut = static_cast<int64_t>(region.fadeOutFrames) + m_dragFadeDelta;
                        const auto clampedFadeOut = static_cast<uint32_t>(std::clamp(newFadeOut, int64_t{0}, static_cast<int64_t>(region.durationFrames)));
                        emit regionFadesChanged(region.id, region.fadeInFrames, clampedFadeOut);
                    }
                }
            } else {
                const int64_t newFadeOut = static_cast<int64_t>(m_dragOrigFadeOut) + m_dragFadeDelta;
                const auto clampedFadeOut = static_cast<uint32_t>(std::max(int64_t{0}, newFadeOut));
                emit regionFadesChanged(m_dragRegionId, m_dragOrigFadeIn, clampedFadeOut);
            }
            break;
        }

        case CanvasDragState::DraggingGainBadge: {
            if (m_selectedRegions.find(m_dragRegionId.toRaw()) != m_selectedRegions.end()) {
                for (uint64_t rawId : m_selectedRegions) {
                    bridge::VisualRegion region;
                    if (m_arrangement->getVisualRegion(bridge::RegionID::fromRaw(rawId), region)) {
                        const float newGain = std::max(0.0f, region.gainLinear + m_dragGainDelta);
                        emit regionGainChanged(region.id, newGain);
                    }
                }
            } else {
                const float newGain = std::max(0.0f, m_dragOrigGain + m_dragGainDelta);
                emit regionGainChanged(m_dragRegionId, newGain);
            }
            break;
        }

        case CanvasDragState::DraggingAutomationTension: {
            m_hudVisible = false;
            break;
        }

        case CanvasDragState::SelectionRubberBand:
        case CanvasDragState::DrawingNewClip:
            // Future: emit selection / new-region signal
            break;

        case CanvasDragState::DraggingControlPoint:
            m_dragPointIndex = -1;
            // Bug 4: clear HUD on release
            m_hudVisible = false;
            m_hudText.clear();
            break;

        default:
            break;
    }

    // Reset state
    m_dragState         = CanvasDragState::Idle;
    m_rubberBandVisible = false;
    m_dragFrameDelta    = 0;
    m_dragGainDelta     = 0.0f;
    m_dragFadeDelta     = 0;
    unsetCursor();

    update();
    event->accept();
}

void PlaylistClipCanvas::keyPressEvent(QKeyEvent* event)
{
    if (hasCompHighlight() && m_compHighlightLane == 0) {
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            deleteClipsInHighlight();
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Alt) {
        if (m_dragState != CanvasDragState::Idle) {
            update();
        }
    }
    QWidget::keyPressEvent(event);
}

void PlaylistClipCanvas::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Alt) {
        if (m_dragState != CanvasDragState::Idle) {
            update();
        }
    }
    QWidget::keyReleaseEvent(event);
}



void PlaylistClipCanvas::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_arrangement) {
        event->accept();
        return;
    }

    const QPoint localPos = event->pos();
    const uint64_t frameAtClick = snapFrame(xToFrame(localPos.x()));

    // Hit-test to see if we clicked a region/clip
    bridge::VisualRegion regions[MAX_VISIBLE];
    const uint32_t count = m_arrangement->getRegionsInViewport(
        m_view.viewStartFrame, m_view.viewEndFrame, regions, MAX_VISIBLE);

    CanvasDragState hitState = CanvasDragState::Idle;
    const int hitIdx = hitTest(localPos, regions, count, hitState);

    QMenu* menu = nullptr;

    if (hitIdx >= 0) {
        // Build clip/region context menu
        menu = PlaylistContextMenu::buildForRegion(
            regions[hitIdx],
            PlaylistEditTool::Select,
            frameAtClick,
            m_tracks,
            m_arrangement,
            this
        );

        if (hasCompHighlight() && m_compHighlightLane == 0
            && m_compHighlightTrack == regions[hitIdx].trackId
            && regions[hitIdx].layerIndex == 0)
        {
            uint64_t start = getCompHighlightStart();
            uint64_t end = start + getCompHighlightLength();
            if (frameAtClick >= start && frameAtClick <= end) {
                menu->addSeparator();
                
                auto splitAct = menu->addAction(QStringLiteral("Split at Selection Edges"));
                QObject::connect(splitAct, &QAction::triggered, [this]() {
                    splitClipsInHighlight();
                });
                
                auto deleteAct = menu->addAction(QStringLiteral("Delete Selection"));
                QObject::connect(deleteAct, &QAction::triggered, [this]() {
                    deleteClipsInHighlight();
                });

                auto consolidateAct = menu->addAction(QStringLiteral("Consolidate Selection"));
                QObject::connect(consolidateAct, &QAction::triggered, [this]() {
                    if (m_arrangement) {
                        m_arrangement->consolidateTrack(m_compHighlightTrack, getCompHighlightStart(), getCompHighlightStart() + getCompHighlightLength());
                    }
                });
            }
        }
    } else {
        // Build empty lane context menu if we right-clicked inside a valid track lane
        const int trackIndex = yToTrackIndex(localPos.y());
        if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
            const auto& track = m_tracks[static_cast<size_t>(trackIndex)];
            double trackY = getTrackYOffset(trackIndex) - static_cast<double>(m_view.verticalOffsetPx);
            double relY = localPos.y() - trackY;

            NodeID dspNode = NodeID::invalid();
            uint32_t subNodeId = 0;
            int32_t parameterIndex = -1;
            if (track.isAutomationExpanded && trackIndex < static_cast<int>(m_view.trackLayouts.size())) {
                const auto& layout = m_view.trackLayouts[static_cast<size_t>(trackIndex)];
                auto hit = layout.hitSubLaneAtY(relY);
                if (hit.index != -1) {
                    dspNode = hit.nodeId;
                    subNodeId = hit.subNodeId;
                    parameterIndex = static_cast<int32_t>(hit.paramIndex);
                }
            }

            bool menuBuilt = false;
            if (dspNode.isValid() && parameterIndex >= 0 && m_automation) {
                bool inHighlight = hasCompHighlight()
                    && m_compHighlightClipType == composition::RegionType::AUTOMATION
                    && m_compHighlightTrack == track.trackId
                    && m_compHighlightNodeId == dspNode
                    && m_compHighlightParamIndex == static_cast<uint32_t>(parameterIndex);

                auto onCopy = [this, track, dspNode, subNodeId, parameterIndex]() {
                    if (!m_automation) return;
                    m_automation->selectActiveAutomationLane(
                        track.trackId, dspNode, subNodeId, parameterIndex);
                    
                    uint64_t s = 0;
                    uint64_t e = 0;
                    if (hasCompHighlight() && m_compHighlightClipType == composition::RegionType::AUTOMATION
                        && m_compHighlightTrack == track.trackId
                        && m_compHighlightNodeId == dspNode
                        && m_compHighlightParamIndex == static_cast<uint32_t>(parameterIndex)) {
                        s = getCompHighlightStart();
                        e = getCompHighlightStart() + getCompHighlightLength();
                    }
                    m_automation->copyAutomationPoints(s, e);
                };

                auto onPaste = [this, track, dspNode, subNodeId, parameterIndex, frameAtClick]() {
                    if (!m_automation) return;
                    m_automation->selectActiveAutomationLane(
                        track.trackId, dspNode, subNodeId, parameterIndex);
                    m_automation->pasteAutomationPoints(frameAtClick);
                    update();
                };

                // Check if the click falls horizontally between two points on this lane
                bridge::VisualAutomationPoint points[1024];
                uint32_t pointCount = m_automation->getCurvePoints(
                    track.trackId, dspNode, subNodeId, static_cast<uint32_t>(parameterIndex),
                    0, UINT64_MAX, points, 1024);

                int leftPointIdx = -1;
                for (uint32_t i = 0; i < pointCount; ++i) {
                    if (points[i].framePosition <= frameAtClick) {
                        leftPointIdx = static_cast<int>(i);
                    } else {
                        break;
                    }
                }

                if (leftPointIdx >= 0 && static_cast<uint32_t>(leftPointIdx) < pointCount - 1
                    && points[leftPointIdx + 1].framePosition > frameAtClick) {
                    uint32_t actualPointIdx = points[leftPointIdx].pointIndex;
                    float currentTension = points[leftPointIdx].tension;
                    auto onShapeChanged = [this, actualPointIdx, currentTension](uint8_t newShape) {
                        if (m_automation) {
                            m_automation->setPointShapeAndTension(actualPointIdx, newShape, currentTension);
                        }
                    };
                    menu = PlaylistContextMenu::buildForAutomationSegment(
                        points[leftPointIdx],
                        onShapeChanged,
                        this
                    );

                    menu->addSeparator();
                    QAction* copyAct = menu->addAction(
                        inHighlight ? QStringLiteral("Copy Highlighted Points")
                                    : QStringLiteral("Copy All Points"));
                    QObject::connect(copyAct, &QAction::triggered, onCopy);

                    QAction* pasteAct = menu->addAction(QStringLiteral("Paste Automation Points"));
                    QObject::connect(pasteAct, &QAction::triggered, onPaste);

                    menuBuilt = true;
                } else {
                    menu = PlaylistContextMenu::buildForAutomationSubLane(inHighlight, onCopy, this);

                    QAction* pasteAct = menu->addAction(QStringLiteral("Paste Automation Points"));
                    QObject::connect(pasteAct, &QAction::triggered, onPaste);

                    menuBuilt = true;
                }
            }

            if (!menuBuilt) {
                if (!dspNode.isValid() || parameterIndex < 0) {
                    menu = PlaylistContextMenu::buildForArrangementLane(
                        track.trackId,
                        track.type,
                        frameAtClick,
                        m_arrangement,
                        this
                    );

                    if (hasCompHighlight() && m_compHighlightLane == 0
                        && m_compHighlightTrack == track.trackId)
                    {
                        uint64_t start = getCompHighlightStart();
                        uint64_t end = start + getCompHighlightLength();
                        if (frameAtClick >= start && frameAtClick <= end) {
                            menu->addSeparator();
                            
                            auto splitAct = menu->addAction(QStringLiteral("Split at Selection Edges"));
                            QObject::connect(splitAct, &QAction::triggered, [this]() {
                                splitClipsInHighlight();
                            });
                            
                            auto deleteAct = menu->addAction(QStringLiteral("Delete Selection"));
                            QObject::connect(deleteAct, &QAction::triggered, [this]() {
                                deleteClipsInHighlight();
                            });

                            auto consolidateAct = menu->addAction(QStringLiteral("Consolidate Selection"));
                            QObject::connect(consolidateAct, &QAction::triggered, [this]() {
                                if (m_arrangement) {
                                    m_arrangement->consolidateTrack(m_compHighlightTrack, getCompHighlightStart(), getCompHighlightStart() + getCompHighlightLength());
                                }
                            });
                        }
                    }
                }
            }
        }
    }

    if (menu) {
        menu->exec(event->globalPos());
        menu->deleteLater();
    }

    event->accept();
}

uint32_t PlaylistClipCanvas::resolvePluginIdFromMime(const QMimeData* mime) const {
    if (mime->hasFormat(bridge::kMimePluginId)) {
        bool ok = false;
        uint32_t pid = static_cast<uint32_t>(
            mime->data(bridge::kMimePluginId).toULongLong(&ok));
        return ok ? pid : UINT32_MAX;
    }
    return UINT32_MAX;
}

void PlaylistClipCanvas::leaveEvent(QEvent* event)
{
    // Bug 4: hide HUD if cursor leaves the widget
    if (m_hudVisible) {
        m_hudVisible = false;
        m_hudText.clear();
        update();
    }
    QWidget::leaveEvent(event);
}

namespace {
bool hasSupportedAudioFile(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls()) {
        return false;
    }
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile()) {
            QString path = url.toLocalFile();
            if (path.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive) ||
                path.endsWith(QStringLiteral(".aif"), Qt::CaseInsensitive) ||
                path.endsWith(QStringLiteral(".aiff"), Qt::CaseInsensitive) ||
                path.endsWith(QStringLiteral(".mp3"), Qt::CaseInsensitive) ||
                path.endsWith(QStringLiteral(".ogg"), Qt::CaseInsensitive) ||
                path.endsWith(QStringLiteral(".flac"), Qt::CaseInsensitive)) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

void PlaylistClipCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (mime->hasFormat(bridge::kMimeMediaId) ||
        mime->hasFormat(bridge::kMimeClipId) ||
        mime->hasFormat(bridge::kMimeClipType)) {
        
        if (mime->hasFormat(bridge::kMimeClipType)) {
            int itemType = mime->data(bridge::kMimeClipType).toInt();
            setActiveDragType(itemType);
        }
        event->acceptProposedAction();
    } else if (hasSupportedAudioFile(mime)) {
        setActiveDragType(static_cast<int>(bridge::BrowserItemType::AudioFile));
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void PlaylistClipCanvas::dragMoveEvent(QDragMoveEvent* event)
{
    const QMimeData* mime = event->mimeData();
    bool isExternalAudio = hasSupportedAudioFile(mime);
    if (!mime->hasFormat(bridge::kMimeMediaId) &&
        !mime->hasFormat(bridge::kMimeClipId) &&
        !mime->hasFormat(bridge::kMimeClipType) &&
        !isExternalAudio) {
        event->ignore();
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointF pos = event->position();
#else
    QPointF pos = event->posF();
#endif

    int trackIndex = yToTrackIndex(pos.y());
    int itemType = -1;
    if (mime->hasFormat(bridge::kMimeClipType)) {
        itemType = mime->data(bridge::kMimeClipType).toInt();
    } else if (isExternalAudio) {
        itemType = static_cast<int>(bridge::BrowserItemType::AudioFile);
    }

    auto acceptOrReject = [&](bool isCompatible) {
        if (isCompatible) {
            setActiveDragType(itemType);
            event->acceptProposedAction();
        } else {
            clearActiveDragType();
            event->ignore();
        }
    };

    // Case A: Cursor is over an existing track
    if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
        const auto& track = m_tracks[static_cast<size_t>(trackIndex)];
        m_dragHoveringEmptySpace = false;

        switch (static_cast<bridge::BrowserItemType>(itemType)) {
        case bridge::BrowserItemType::AudioFile:
            acceptOrReject(track.type == composition::TrackType::AUDIO);
            return;
        case bridge::BrowserItemType::MidiFile:
            acceptOrReject(track.type == composition::TrackType::MIDI ||
                           track.type == composition::TrackType::INSTRUMENT);
            return;
        case bridge::BrowserItemType::PluginGenerator:
            acceptOrReject(track.type == composition::TrackType::MIDI ||
                           track.type == composition::TrackType::INSTRUMENT);
            return;
        case bridge::BrowserItemType::PluginEffect:
            acceptOrReject(true);  // any track can host effects
            return;
        default:
            acceptOrReject(false);
            return;
        }
    }

    // Case B: Cursor is on empty space (below all tracks or no tracks exist)
    // Accept AudioFile and PluginGenerator here for "create new track" flow.
    m_dragHoveringEmptySpace = true;
    switch (static_cast<bridge::BrowserItemType>(itemType)) {
    case bridge::BrowserItemType::AudioFile:
    case bridge::BrowserItemType::PluginGenerator:
        setActiveDragType(itemType);
        event->acceptProposedAction();
        return;
    default:
        clearActiveDragType();
        event->ignore();
        return;
    }
}

void PlaylistClipCanvas::dragLeaveEvent(QDragLeaveEvent* event)
{
    clearActiveDragType();
    m_dragHoveringEmptySpace = false;
    event->accept();
}

void PlaylistClipCanvas::dropEvent(QDropEvent* event)
{
    clearActiveDragType();
    m_dragHoveringEmptySpace = false;

    const QMimeData* mime = event->mimeData();
    bool isExternalAudio = hasSupportedAudioFile(mime);
    if (!mime->hasFormat(bridge::kMimeClipType) && !isExternalAudio) {
        event->ignore();
        return;
    }

    int itemType = -1;
    if (mime->hasFormat(bridge::kMimeClipType)) {
        itemType = mime->data(bridge::kMimeClipType).toInt();
    } else if (isExternalAudio) {
        itemType = static_cast<int>(bridge::BrowserItemType::AudioFile);
    }

    uint64_t mediaIdRaw = 0;
    if (!isExternalAudio) {
        if (mime->hasFormat(bridge::kMimeMediaId)) {
            mediaIdRaw = mime->data(bridge::kMimeMediaId).toULongLong();
        } else if (mime->hasFormat(bridge::kMimeClipId)) {
            mediaIdRaw = mime->data(bridge::kMimeClipId).toULongLong();
        } else {
            event->ignore();
            return;
        }
    }

    uint32_t pluginId = resolvePluginIdFromMime(mime);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointF pos = event->position();
#else
    QPointF pos = event->posF();
#endif

    int trackIndex = yToTrackIndex(pos.y());
    uint64_t dropFrame = snapFrame(xToFrame(pos.x()));

    auto bItemType = static_cast<bridge::BrowserItemType>(itemType);

    // ── CASE A: Drop on empty space (create new track flow) ──────────────
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        switch (bItemType) {
        case bridge::BrowserItemType::AudioFile: {
            if (isExternalAudio) {
                QStringList filePaths;
                for (const QUrl& url : mime->urls()) {
                    if (url.isLocalFile()) {
                        QString filePath = url.toLocalFile();
                        if (filePath.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive) ||
                            filePath.endsWith(QStringLiteral(".aif"), Qt::CaseInsensitive) ||
                            filePath.endsWith(QStringLiteral(".aiff"), Qt::CaseInsensitive) ||
                            filePath.endsWith(QStringLiteral(".mp3"), Qt::CaseInsensitive) ||
                            filePath.endsWith(QStringLiteral(".ogg"), Qt::CaseInsensitive) ||
                            filePath.endsWith(QStringLiteral(".flac"), Qt::CaseInsensitive)) {
                            filePaths.append(filePath);
                        }
                    }
                }
                if (!filePaths.isEmpty()) {
                    emit addAudioTracksWithClipsRequested(filePaths, dropFrame);
                    event->acceptProposedAction();
                } else {
                    event->ignore();
                }
            } else {
                std::string filePath;
                if (m_browser && m_browser->getMediaPath(
                        MediaID::fromRaw(mediaIdRaw), filePath)) {
                    emit addAudioTracksWithClipsRequested(
                        QStringList{QString::fromStdString(filePath)}, dropFrame);
                    event->acceptProposedAction();
                } else {
                    event->ignore();
                }
            }
            return;
        }
        case bridge::BrowserItemType::PluginGenerator:
            if (pluginId != UINT32_MAX) {
                emit addInstrumentTrackWithPluginRequested(pluginId);
                event->acceptProposedAction();
            } else {
                event->ignore();
            }
            return;
        default:
            event->ignore();
            return;
        }
    }

    // ── CASE B: Drop on existing track ───────────────────────────────────
    const auto& targetTrackState = m_tracks[static_cast<size_t>(trackIndex)];
    TrackID targetTrack = targetTrackState.trackId;

    switch (bItemType) {
    case bridge::BrowserItemType::AudioFile: {
        if (targetTrackState.type != composition::TrackType::AUDIO) {
            event->ignore();
            return;
        }
        std::vector<bridge::RegionID> newRegions;
        if (isExternalAudio) {
            bool accepted = false;
            for (const QUrl& url : mime->urls()) {
                if (url.isLocalFile()) {
                    QString filePath = url.toLocalFile();
                    if (filePath.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive) ||
                        filePath.endsWith(QStringLiteral(".aif"), Qt::CaseInsensitive) ||
                        filePath.endsWith(QStringLiteral(".aiff"), Qt::CaseInsensitive) ||
                        filePath.endsWith(QStringLiteral(".mp3"), Qt::CaseInsensitive) ||
                        filePath.endsWith(QStringLiteral(".ogg"), Qt::CaseInsensitive) ||
                        filePath.endsWith(QStringLiteral(".flac"), Qt::CaseInsensitive)) {
                        if (m_arrangement) {
                            bridge::RegionID rId = m_arrangement->importAudioClip(
                                targetTrack, filePath.toUtf8().constData(), dropFrame);
                            if (rId.isValid()) {
                                newRegions.push_back(rId);
                            }
                            update();
                        }
                        accepted = true;
                    }
                }
            }
            if (accepted) {
                event->acceptProposedAction();
            } else {
                event->ignore();
            }
        } else {
            std::string filePath;
            if (m_browser && m_browser->getMediaPath(
                    MediaID::fromRaw(mediaIdRaw), filePath)) {
                if (m_arrangement) {
                    bridge::RegionID rId = m_arrangement->importAudioClip(
                        targetTrack, filePath.c_str(), dropFrame);
                    if (rId.isValid()) {
                        newRegions.push_back(rId);
                    }
                    update();
                }
                event->acceptProposedAction();
            } else {
                // Fallback to name if path lookup fails
                if (mime->hasFormat(bridge::kMimeClipName)) {
                    QString name = QString::fromUtf8(mime->data(bridge::kMimeClipName));
                    filePath = name.toStdString();
                    if (m_arrangement) {
                        bridge::RegionID rId = m_arrangement->importAudioClip(targetTrack, filePath.c_str(), dropFrame);
                        if (rId.isValid()) {
                            newRegions.push_back(rId);
                        }
                        update();
                    }
                    event->acceptProposedAction();
                } else {
                    event->ignore();
                }
            }
        }

        if (!newRegions.empty()) {
            selectRegions(newRegions);
        }
        emit trackSelectionRequested(targetTrack, false, false);
        return;
    }

    case bridge::BrowserItemType::MidiFile: {
        if (targetTrackState.type != composition::TrackType::MIDI &&
            targetTrackState.type != composition::TrackType::INSTRUMENT) {
            event->ignore();
            return;
        }
        if (m_arrangement) {
            uint64_t defaultMidiDuration = 44100 * 4; // 4 bars
            m_arrangement->insertMidiClip(
                targetTrack, dropFrame, defaultMidiDuration);
            update();
        }
        event->acceptProposedAction();
        return;
    }

    case bridge::BrowserItemType::PluginGenerator: {
        if (targetTrackState.type != composition::TrackType::MIDI &&
            targetTrackState.type != composition::TrackType::INSTRUMENT) {
            event->ignore();
            return;
        }
        if (pluginId != UINT32_MAX) {
            emit insertInstrumentRequested(targetTrack, pluginId);
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
        return;
    }

    case bridge::BrowserItemType::PluginEffect: {
        if (pluginId != UINT32_MAX) {
            emit insertPluginRequested(targetTrack, pluginId);
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
        return;
    }

    default:
        event->ignore();
        return;
    }
}

void PlaylistClipCanvas::selectAll() {
    if (!m_arrangement || m_view.viewEndFrame <= m_view.viewStartFrame) return;
    bridge::VisualRegion regions[MAX_VISIBLE];
    const uint32_t count = m_arrangement->getRegionsInViewport(
        m_view.viewStartFrame, m_view.viewEndFrame, regions, MAX_VISIBLE);
    for (uint32_t i = 0; i < count; ++i) {
        m_selectedRegions.insert(regions[i].id.toRaw());
    }
    update();
}

void PlaylistClipCanvas::deselectAll() {
    m_selectedRegions.clear();
    m_lastSelectedRegionId = bridge::RegionID::invalid();
    update();
}


void PlaylistClipCanvas::selectRegions(const std::vector<bridge::RegionID>& ids) {
    m_selectedRegions.clear();
    for (const auto& id : ids) {
        if (id.isValid()) {
            m_selectedRegions.insert(id.toRaw());
            m_lastSelectedRegionId = id;
        }
    }
    update();
}

uint64_t PlaylistClipCanvas::snapFrame(uint64_t frame, int roundDir) const
{
    if (!m_inputMode || !m_timeline) {
        return frame;
    }

    if (QGuiApplication::keyboardModifiers() & Qt::AltModifier) {
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

        if (roundDir > 0 && frame > currentBarFrame) return nextBarFrame;
        if (roundDir < 0) return currentBarFrame;

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
    
    if (roundDir > 0 && ticks > low) return m_timeline->ticksToSamples(high);
    if (roundDir < 0) return m_timeline->ticksToSamples(low);

    const uint64_t snappedTicks = (ticks - low < high - ticks) ? low : high;
    return m_timeline->ticksToSamples(snappedTicks);
}
void PlaylistClipCanvas::onTileRendered(presentation::views::TileKey key, QImage image)
{
    if (image.isNull()) {
        QTimer::singleShot(300, this, [this, key]() {
            m_inFlightTiles.erase(key);
            update();
        });
    } else {
        m_inFlightTiles.erase(key);
        m_tileCache.insert(key, QPixmap::fromImage(image));
        update();
    }
}

void PlaylistClipCanvas::prefetchWaveformTiles()
{
    if (!m_arrangement || !m_tileWorker) return;

    const uint64_t viewWidth = m_view.viewEndFrame - m_view.viewStartFrame;
    if (viewWidth == 0) return;

    const uint64_t prefetchStart = (m_view.viewStartFrame > viewWidth) ? m_view.viewStartFrame - viewWidth : 0;
    const uint64_t prefetchEnd = m_view.viewEndFrame + viewWidth;

    bridge::VisualRegion regions[MAX_VISIBLE];
    const uint32_t count = m_arrangement->getRegionsInViewport(
        prefetchStart, prefetchEnd, regions, MAX_VISIBLE);

    const double zoomFactor = m_view.zoomFactor;
    const double spp_screen = 1.0 / zoomFactor;

    for (uint32_t i = 0; i < count; ++i) {
        const auto& region = regions[i];
        if (region.clipType != composition::RegionType::AUDIO) {
            continue;
        }

        const double ratio = static_cast<double>(region.playbackRatio > 0.0f ? region.playbackRatio : 1.0f);
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

        // Overlap of region with the prefetch window
        const uint64_t clipStartFrame = region.startFrame;
        const uint64_t clipEndFrame = region.startFrame + region.durationFrames;
        const uint64_t overlapStart = std::max(clipStartFrame, prefetchStart);
        const uint64_t overlapEnd = std::min(clipEndFrame, prefetchEnd);

        if (overlapEnd > overlapStart) {
            const uint64_t fileStart = static_cast<uint64_t>(
                static_cast<double>((overlapStart - clipStartFrame) + region.fileOffsetFrames) * ratio);
            const uint64_t fileEnd = static_cast<uint64_t>(
                static_cast<double>((overlapEnd - clipStartFrame) + region.fileOffsetFrames) * ratio);

            const int r = static_cast<int>((region.colorARGB >> 16) & 0xFF);
            const int g = static_cast<int>((region.colorARGB >>  8) & 0xFF);
            const int b = static_cast<int>( region.colorARGB        & 0xFF);
            const QColor waveColor(r, g, b, region.isMuted ? 75 : 220);
            const uint32_t waveColorARGB = waveColor.rgba();

            const uint64_t tileFrameCount = 256ULL * quantizedZoom;
            const uint32_t tileX_min = static_cast<uint32_t>(fileStart / tileFrameCount);
            const uint32_t tileX_max = static_cast<uint32_t>(fileEnd / tileFrameCount);

            // Determine lane/waveRect height
            int trackRow = -1;
            for (int t = 0; t < static_cast<int>(m_tracks.size()); ++t) {
                if (m_tracks[static_cast<size_t>(t)].trackId == region.trackId) {
                    trackRow = t;
                    break;
                }
            }
            if (trackRow < 0) continue;
            if (trackRow >= static_cast<int>(m_view.trackLayouts.size())) continue;
            const double laneH = m_view.trackLayouts[static_cast<size_t>(trackRow)].mainLaneHeight - 1.0;
            const double waveH = laneH - 16.0 - 1.0; // Inset same as paint path
            if (waveH <= 4.0) continue;

            for (uint32_t tileX = tileX_min; tileX <= tileX_max; ++tileX) {
                TileKey key{
                    .mediaId = region.mediaId,
                    .zoomTier = quantizedZoom,
                    .tileX = tileX,
                    .colorARGB = waveColorARGB,
                    .generation = 0
                };

                if (!m_tileCache.lookup(key) && m_inFlightTiles.find(key) == m_inFlightTiles.end()) {
                    // Enqueue prefetch request
                    const uint64_t tileStartF = tileX * tileFrameCount;
                    const uint64_t tileEndF = tileStartF + tileFrameCount;

                    TileRequest req{
                        .key = key,
                        .fileStartFrame = tileStartF,
                        .fileEndFrame = tileEndF,
                        .pixelWidth = 256,
                        .pixelHeight = static_cast<uint32_t>(std::ceil(waveH))
                    };
                    if (m_tileWorker->enqueue(req)) {
                        m_inFlightTiles.insert(key);
                    }
                }
            }
        }
    }
}





// ─────────────────────────────────────────────────────────────────────────────
// HUD tooltip overlay — rendered above clips during control-point drag
// ─────────────────────────────────────────────────────────────────────────────
void PlaylistClipCanvas::drawHUD(QPainter& p)
{
    if (!m_hudVisible || m_hudText.isEmpty()) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    const QFont font = theme::Font::monospace(9, QFont::Bold);
    p.setFont(font);
    const QFontMetricsF fm(font);
    const QRectF textRect = fm.boundingRect(m_hudText);

    const double padX = 8.0;
    const double padY = 4.0;
    const QRectF bubble(
        m_hudPos.x(),
        m_hudPos.y() - textRect.height() - padY * 2.0,
        textRect.width() + padX * 2.0,
        textRect.height() + padY * 2.0
    );

    // Clamp bubble to widget bounds so it never clips off-screen
    const double maxX = static_cast<double>(width())  - bubble.width()  - 2.0;
    const double maxY = static_cast<double>(height()) - bubble.height() - 2.0;
    const QRectF clampedBubble(
        std::min(bubble.x(), maxX),
        std::max(2.0, std::min(bubble.y(), maxY)),
        bubble.width(),
        bubble.height()
    );

    // Glassmorphism dark bubble
    QColor hudBg = theme::Color::BgBase;
    hudBg.setAlpha(225);
    QColor hudBorder = theme::Color::AccentGlow;
    hudBorder.setAlpha(160);
    p.setBrush(hudBg);
    p.setPen(QPen(hudBorder, 1.0));
    p.drawRoundedRect(clampedBubble, 4.0, 4.0);

    // Cyan value text
    p.setPen(theme::Color::AccentGlow);
    p.drawText(clampedBubble, Qt::AlignCenter, m_hudText);

    p.restore();
}

void PlaylistClipCanvas::splitClipsInHighlight()
{
    if (!hasCompHighlight() || m_compHighlightLane != 0 || !m_arrangement) return;

    uint64_t start = getCompHighlightStart();
    uint64_t end = start + getCompHighlightLength();

    auto getRegionAt = [&](uint64_t frame) -> bridge::VisualRegion {
        bridge::VisualRegion scratch[MAX_VISIBLE];
        const uint32_t count = m_arrangement->getRegionsInViewport(
            m_view.viewStartFrame, m_view.viewEndFrame, scratch, MAX_VISIBLE);
        for (uint32_t i = 0; i < count; ++i) {
            if (scratch[i].trackId == m_compHighlightTrack && scratch[i].layerIndex == 0) {
                if (frame > scratch[i].startFrame && frame < scratch[i].startFrame + scratch[i].durationFrames) {
                    return scratch[i];
                }
            }
        }
        bridge::VisualRegion empty{};
        empty.id = bridge::RegionID::invalid();
        return empty;
    };

    // Split at start border if needed
    auto rStart = getRegionAt(start);
    if (rStart.id.isValid()) {
        m_arrangement->splitRegion(rStart.id, start);
    }

    // Split at end border if needed
    auto rEnd = getRegionAt(end);
    if (rEnd.id.isValid()) {
        m_arrangement->splitRegion(rEnd.id, end);
    }

    clearCompHighlight();
}

void PlaylistClipCanvas::deleteClipsInHighlight()
{
    if (!hasCompHighlight() || m_compHighlightLane != 0 || !m_arrangement) return;

    uint64_t start = getCompHighlightStart();
    uint64_t end = start + getCompHighlightLength();

    auto getRegionAt = [&](uint64_t frame) -> bridge::VisualRegion {
        bridge::VisualRegion scratch[MAX_VISIBLE];
        const uint32_t count = m_arrangement->getRegionsInViewport(
            m_view.viewStartFrame, m_view.viewEndFrame, scratch, MAX_VISIBLE);
        for (uint32_t i = 0; i < count; ++i) {
            if (scratch[i].trackId == m_compHighlightTrack && scratch[i].layerIndex == 0) {
                if (frame > scratch[i].startFrame && frame < scratch[i].startFrame + scratch[i].durationFrames) {
                    return scratch[i];
                }
            }
        }
        bridge::VisualRegion empty{};
        empty.id = bridge::RegionID::invalid();
        return empty;
    };

    // Split at start border if needed
    auto rStart = getRegionAt(start);
    if (rStart.id.isValid()) {
        m_arrangement->splitRegion(rStart.id, start);
    }

    // Split at end border if needed
    auto rEnd = getRegionAt(end);
    if (rEnd.id.isValid()) {
        m_arrangement->splitRegion(rEnd.id, end);
    }

    // Now query all regions again and delete any fully inside [start, end]
    bridge::VisualRegion scratch[MAX_VISIBLE];
    const uint32_t count = m_arrangement->getRegionsInViewport(
        m_view.viewStartFrame, m_view.viewEndFrame, scratch, MAX_VISIBLE);
    for (uint32_t i = 0; i < count; ++i) {
        if (scratch[i].trackId == m_compHighlightTrack && scratch[i].layerIndex == 0) {
            uint64_t regStart = scratch[i].startFrame;
            uint64_t regEnd = regStart + scratch[i].durationFrames;
            if (regStart >= start && regEnd <= end) {
                m_tileCache.invalidateMedia(MediaID::fromRaw(scratch[i].mediaId));
                m_arrangement->deleteRegion(scratch[i].id);
            }
        }
    }

    clearCompHighlight();
}

void PlaylistClipCanvas::splitClipsAtPlayhead(uint64_t playheadFrame)
{
    if (!m_arrangement || m_selectedRegions.empty()) return;
    std::vector<uint64_t> toSplit(m_selectedRegions.begin(), m_selectedRegions.end());
    for (uint64_t rawId : toSplit) {
        bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
        bridge::VisualRegion r{};
        if (m_arrangement->getVisualRegion(id, r)) {
            if (playheadFrame > r.startFrame && playheadFrame < r.startFrame + r.durationFrames) {
                m_arrangement->splitRegion(id, playheadFrame);
            }
        }
    }
    update();
}

void PlaylistClipCanvas::deleteSelectedClips()
{
    if (!m_arrangement || m_selectedRegions.empty()) return;
    std::vector<uint64_t> toDelete(m_selectedRegions.begin(), m_selectedRegions.end());
    m_selectedRegions.clear();
    for (uint64_t rawId : toDelete) {
        bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
        bridge::VisualRegion r{};
        if (m_arrangement->getVisualRegion(id, r)) {
            m_tileCache.invalidateMedia(MediaID::fromRaw(r.mediaId));
        }
        m_arrangement->deleteRegion(id);
    }
    update();
}

void PlaylistClipCanvas::duplicateSelectedClips()
{
    if (!m_arrangement || m_selectedRegions.empty()) return;
    std::vector<uint64_t> original(m_selectedRegions.begin(), m_selectedRegions.end());
    std::vector<bridge::RegionID> newlyCreated;
    for (uint64_t rawId : original) {
        bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
        bridge::VisualRegion r{};
        if (m_arrangement->getVisualRegion(id, r)) {
            uint64_t targetStart = r.startFrame + r.durationFrames;
            bridge::RegionID newId = bridge::RegionID::invalid();
            if (r.clipType == composition::RegionType::AUDIO) {
                newId = m_arrangement->importAudioClip(r.trackId, r.comment, targetStart);
            } else if (r.clipType == composition::RegionType::MIDI) {
                newId = m_arrangement->insertMidiClip(r.trackId, targetStart, r.durationFrames);
            } else if (r.clipType == composition::RegionType::AUTOMATION) {
                newId = m_arrangement->insertAutomationClip(r.trackId, r.automationTargetNodeId, r.automationParameterIndex, targetStart, r.durationFrames);
            }
            if (newId.isValid()) {
                newlyCreated.push_back(newId);
            }
        }
    }
    selectRegions(newlyCreated);
    update();
}

void PlaylistClipCanvas::toggleMuteSelectedClips()
{
    if (!m_arrangement || m_selectedRegions.empty()) return;
    for (uint64_t rawId : m_selectedRegions) {
        bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
        bridge::VisualRegion r{};
        if (m_arrangement->getVisualRegion(id, r)) {
            m_arrangement->setRegionMuted(id, !r.isMuted);
        }
    }
    update();
}

void PlaylistClipCanvas::quantizeSelectedClips()
{
    if (!m_arrangement || !m_timeline || m_selectedRegions.empty()) return;
    uint64_t snapFrames = m_timeline->ticksToSamples(60); // default 1/16
    if (snapFrames == 0) snapFrames = 1;
    for (uint64_t rawId : m_selectedRegions) {
        bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
        bridge::VisualRegion r{};
        if (m_arrangement->getVisualRegion(id, r)) {
            uint64_t quantizedStart = ((r.startFrame + (snapFrames / 2)) / snapFrames) * snapFrames;
            m_arrangement->moveRegion(id, r.trackId, static_cast<int64_t>(quantizedStart));
        }
    }
    update();
}

void PlaylistClipCanvas::consolidateSelectedClips()
{
    if (!m_arrangement || m_selectedRegions.empty()) return;
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> trackBounds;
    for (uint64_t rawId : m_selectedRegions) {
        bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
        bridge::VisualRegion r{};
        if (m_arrangement->getVisualRegion(id, r)) {
            uint64_t tr = r.trackId.toRaw();
            uint64_t endF = r.startFrame + r.durationFrames;
            if (trackBounds.find(tr) == trackBounds.end()) {
                trackBounds[tr] = {r.startFrame, endF};
            } else {
                trackBounds[tr].first = std::min(trackBounds[tr].first, r.startFrame);
                trackBounds[tr].second = std::max(trackBounds[tr].second, endF);
            }
        }
    }
    for (const auto& [trRaw, bounds] : trackBounds) {
        m_arrangement->consolidateTrack(composition::uint64ToHandle<TrackID>(trRaw), bounds.first, bounds.second);
    }
    update();
}

bool PlaylistClipCanvas::getSelectedRange(uint64_t& outStartFrame, uint64_t& outEndFrame) const
{
    if (!m_arrangement || m_selectedRegions.empty()) return false;
    outStartFrame = UINT64_MAX;
    outEndFrame = 0;
    for (uint64_t rawId : m_selectedRegions) {
        bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
        bridge::VisualRegion r{};
        if (m_arrangement->getVisualRegion(id, r)) {
            outStartFrame = std::min(outStartFrame, r.startFrame);
            outEndFrame = std::max(outEndFrame, r.startFrame + r.durationFrames);
        }
    }
    return outStartFrame < outEndFrame;
}

void PlaylistClipCanvas::nudgeSelectedClips(int64_t deltaFrames)
{
    if (!m_arrangement || m_selectedRegions.empty()) return;
    for (uint64_t rawId : m_selectedRegions) {
        bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
        bridge::VisualRegion r{};
        if (m_arrangement->getVisualRegion(id, r)) {
            int64_t newStart = std::max<int64_t>(0, static_cast<int64_t>(r.startFrame) + deltaFrames);
            m_arrangement->moveRegion(id, r.trackId, newStart);
        }
    }
    update();
}

void PlaylistClipCanvas::moveSelectedClipsTrack(int trackOffset)
{
    if (!m_arrangement || m_tracks.empty() || m_selectedRegions.empty()) return;
    for (uint64_t rawId : m_selectedRegions) {
        bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
        bridge::VisualRegion r{};
        if (m_arrangement->getVisualRegion(id, r)) {
            int currentIdx = -1;
            for (size_t i = 0; i < m_tracks.size(); ++i) {
                if (m_tracks[i].trackId == r.trackId) {
                    currentIdx = static_cast<int>(i);
                    break;
                }
            }
            if (currentIdx >= 0) {
                int targetIdx = std::clamp(currentIdx + trackOffset, 0, static_cast<int>(m_tracks.size()) - 1);
                m_arrangement->moveRegion(id, m_tracks[static_cast<size_t>(targetIdx)].trackId, static_cast<int64_t>(r.startFrame));
            }
        }
    }
    update();
}

void PlaylistClipCanvas::renameSelectedClip()
{
    if (!m_arrangement || m_selectedRegions.empty()) return;
    uint64_t rawId = *m_selectedRegions.begin();
    bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
    bridge::VisualRegion r{};
    if (m_arrangement->getVisualRegion(id, r)) {
        bool ok = false;
        QString newName = DAWInputDialog::getText(this, QStringLiteral("Rename Clip"),
                                                QStringLiteral("New Clip Name:"),
                                                QString::fromUtf8(r.name), &ok);
        if (ok && !newName.isEmpty()) {
            m_arrangement->updateRegionMetadata(id, newName.toUtf8().constData(), r.comment, r.colorARGB);
            update();
        }
    }
}

void PlaylistClipCanvas::openFadeConfigDialog()
{
    if (!m_arrangement || m_selectedRegions.empty()) return;
    uint64_t rawId = *m_selectedRegions.begin();
    bridge::RegionID id = composition::uint64ToHandle<bridge::RegionID>(rawId);
    bridge::VisualRegion r{};
    if (m_arrangement->getVisualRegion(id, r)) {
        bool ok = false;
        double fadeIn = DAWInputDialog::getDouble(this, QStringLiteral("Fade In (ms)"), QStringLiteral("Fade In Duration (ms):"), static_cast<double>(r.fadeInFrames / 44.0), 0.0, 10000.0, 2, &ok);
        if (ok) {
            m_arrangement->setRegionFades(id, static_cast<uint32_t>(fadeIn * 44.0), r.fadeOutFrames);
            update();
        }
    }
}

void PlaylistClipCanvas::invertSelection()
{
    if (!m_arrangement) return;
    bridge::VisualRegion scratch[MAX_VISIBLE];
    const uint32_t count = m_arrangement->getRegionsInViewport(
        m_view.viewStartFrame, m_view.viewEndFrame, scratch, MAX_VISIBLE);
    std::unordered_set<uint64_t> newSel;
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t rawId = scratch[i].id.toRaw();
        if (m_selectedRegions.find(rawId) == m_selectedRegions.end()) {
            newSel.insert(rawId);
        }
    }
    m_selectedRegions = std::move(newSel);
    update();
}

} // namespace presentation::views
