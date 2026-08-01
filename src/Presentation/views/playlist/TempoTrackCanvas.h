// src/Presentation/views/playlist/TempoTrackCanvas.h
#pragma once

#include <QWidget>
#include "timeline/itimeline_controller.h"
#include "engine/iinput_mode_controller.h"
#include "timeline/iarrangement_controller.h"
#include <vector>

namespace presentation::views {

class TempoTrackCanvas : public QWidget {
    Q_OBJECT

public:
    explicit TempoTrackCanvas(bridge::ITimelineController* timeline,
                              bridge::IInputModeController* inputMode,
                              bridge::IArrangementController* arrangement,
                              QWidget* parent = nullptr);
    ~TempoTrackCanvas() override = default;

    void setViewState(uint64_t startFrame, uint64_t endFrame, double zoomFactor);
    void refreshTimelineCache();

signals:
    void heightResizeRequested(int newHeight);
    void zoomScrollChanged(uint64_t newStart, double newZoom);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void doubleClickEvent(QMouseEvent* event); // We will intercept mouseDoubleClickEvent
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    // Mappings
    uint64_t xToFrame(double x) const;
    double frameToX(uint64_t frame) const;
    double bpmToY(double bpm) const;
    double yToBpm(double y) const;
    uint64_t snapFrame(uint64_t frame) const;

    int findNodeUnderMouse(const QPointF& pos) const;
    bool isInResizeZone(const QPointF& pos) const;
    void drawGrid(QPainter& p);

    bridge::ITimelineController* m_timeline{nullptr};
    bridge::IInputModeController* m_inputMode{nullptr};
    bridge::IArrangementController* m_arrangement{nullptr};

    // Viewport state
    uint64_t m_startFrame{0};
    uint64_t m_endFrame{0};
    double m_zoomFactor{0.001};

    // Cached tempo points
    static constexpr uint32_t MAX_TEMPO_POINTS = 256;
    bridge::VisualMarker m_points[MAX_TEMPO_POINTS]; // Reusing VisualMarker to hold point data: framePosition, label (contains BPM string), colorARGB
    
    struct LocalTempoPoint {
        uint64_t frame;
        double bpm;
    };
    std::vector<LocalTempoPoint> m_tempoPoints;

    // Interaction states
    enum class InteractionMode {
        None,
        DraggingNode,
        ResizingHeight
    };
    InteractionMode m_interaction{InteractionMode::None};
    int m_activeNodeIdx{-1};
    uint64_t m_dragStartFrame{0};
    double m_dragStartBpm{120.0};
    QPointF m_dragAnchor;
    int m_dragStartHeight{80};
    bool m_collapsed{false};
};

} // namespace presentation::views
