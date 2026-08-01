// src/Presentation/views/playlist/TempoTrackHeader.h
#pragma once

#include <QWidget>
#include "timeline/itimeline_controller.h"

namespace presentation::views {

class TempoTrackHeader : public QWidget {
    Q_OBJECT

public:
    explicit TempoTrackHeader(bridge::ITimelineController* timeline, QWidget* parent = nullptr);
    ~TempoTrackHeader() override = default;

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

    void updateBpmReadout(double bpm);

signals:
    void collapseToggled(bool collapsed);
    void heightResizeRequested(int newHeight);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool isInResizeZone(const QPointF& pos) const;

    bridge::ITimelineController* m_timeline{nullptr};
    bool m_collapsed{false};
    double m_currentBpm{120.0};

    QRectF m_chevronRect;

    // Resizing interaction state
    bool m_isResizing{false};
    QPointF m_dragAnchor;
    int m_dragStartHeight{80};
};

} // namespace presentation::views
