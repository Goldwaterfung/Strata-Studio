// src/Presentation/views/playlist/PlaylistTimelineRuler.h
#pragma once

#include <QWidget>
#include "timeline/itimeline_controller.h"
#include "engine/iinput_mode_controller.h"
#include "timeline/iarrangement_controller.h"
#include "PlaylistViewport.h"

namespace presentation::views {

/**
 * @brief Premium timeline ruler widget for the Playlist view.
 *
 * Responsibilities:
 *  - Renders the musical bar/beat divisions based on the BPM and sample rate.
 *  - Displays named project markers (VisualMarker) and the loop range highlight.
 *  - Renders the transport playhead handle.
 *  - Handles click-to-seek and drag-to-scrub interactions.
 *  - Provides zoom/scroll interaction via middle-drag, right-drag, or Shift+drag.
 *
 * Threading/Rendering laws:
 *  - No bridge calls are ever made inside paintEvent().
 *  - All state (playhead, loop range, markers) is cached inside local member variables.
 *  - setPlayheadFrame() is called by the 60 Hz GUI tick.
 *  - Double-precision math is used exclusively for coordinate calculations.
 */
class PlaylistTimelineRuler : public QWidget {
    Q_OBJECT

public:
    explicit PlaylistTimelineRuler(bridge::ITimelineController* timeline,
                                   bridge::IInputModeController* inputMode,
                                   bridge::IArrangementController* arrangement,
                                   QWidget* parent = nullptr);
    ~PlaylistTimelineRuler() override = default;

    /**
     * @brief Update the cached scroll/zoom view state.
     */
    void setViewState(uint64_t startFrame, uint64_t endFrame, double zoomFactor);

    /**
     * @brief Update the playhead frame. Repaints only if playhead's pixel coordinate shifts.
     */
    void setPlayheadFrame(uint64_t frame);

    /**
     * @brief Update the cached loop range and enabled state.
     */
    void setLoopState(bool enabled, uint64_t start, uint64_t end);

    /**
     * @brief Refreshes cached loop range and named markers from the bridge.
     *        Called during initialization and on session/marker mutations.
     */
    void refreshTimelineCache();

signals:
    void seekRequested(uint64_t frame);
    void loopRangeChanged(uint64_t start, uint64_t end);
    void zoomScrollChanged(uint64_t newStart, double newZoom);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void drawBarGrid(QPainter& p);
    void drawLoopRegion(QPainter& p);
    void drawNamedMarkers(QPainter& p);
    void drawPlayhead(QPainter& p);

    // Coordinate mapping helpers (double precision)
    uint64_t xToFrame(double x) const;
    double frameToX(uint64_t frame) const;

private:
    bridge::ITimelineController* m_timeline{nullptr};
    bridge::IInputModeController* m_inputMode{nullptr};
    bridge::IArrangementController* m_arrangement{nullptr};

    uint64_t snapFrame(uint64_t frame) const;

    // Cached viewport state
    uint64_t m_startFrame{0};
    uint64_t m_endFrame{0};
    double   m_zoomFactor{0.001};

    // Cached playback state
    uint64_t m_playheadFrame{0};
    double   m_cachedPlayheadX{-1.0};

    // Cached loop state
    uint64_t m_loopStart{0};
    uint64_t m_loopEnd{0};
    bool     m_loopEnabled{false};

    // Cached named markers
    static constexpr uint32_t MAX_MARKERS = 64;
    bridge::VisualMarker      m_markers[MAX_MARKERS];
    uint32_t                  m_markerCount{0};

    // Interaction state machine
    enum class InteractionMode {
        None,
        Seeking,          ///< Left click seek & scrub
        DraggingLoopStart,///< Adjusting left loop boundary
        DraggingLoopEnd,  ///< Adjusting right loop boundary
        ZoomScrolling,    ///< Right/middle or Shift+left drag zoom & scroll
        DrawingLoopRange, ///< Gesture for drawing loop range from scratch
        DraggingMarker    ///< Dragging marker on ruler
    };
    InteractionMode m_interaction{InteractionMode::None};
    QPointF         m_dragAnchorPos;
    uint64_t        m_dragStartFrame{0};
    double          m_dragStartZoom{0.001};
    uint64_t        m_dragStartLoopStart{0};
    uint64_t        m_dragStartLoopEnd{0};
    MarkerUUID      m_draggedMarkerUuid;
};

} // namespace presentation::views
