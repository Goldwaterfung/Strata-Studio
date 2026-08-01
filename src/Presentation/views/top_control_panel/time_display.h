// src/Presentation/views/top_control_panel/time_display.h
#pragma once

#include <QWidget>
#include <QPointF>
#include <cstdint>

class QDoubleSpinBox;

namespace bridge {
class ITimelineController;
}

namespace presentation::views {

/**
 * @brief Custom-painted time display widget showing Bar:Beat:Tick or MM:SS:CS.
 *
 * Click to toggle between BBT and absolute time display modes.
 * Styled with the Cyber-Industrial theme (dark glass panel, green accent text).
 */
class TimeDisplay : public QWidget {
    Q_OBJECT

public:
    explicit TimeDisplay(QWidget* parent = nullptr);
    ~TimeDisplay() override = default;

    void bind(bridge::ITimelineController* controller);

    /**
     * @brief Update the displayed position. Called at 60Hz from PresentationDirector.
     */
    void updatePosition(uint32_t bar, uint32_t beat, uint32_t tick,
                        double seconds, double bpm);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private Q_SLOTS:
    void onBPMEditingFinished();

private:
    QString formatBBT() const;
    QString formatAbsolute() const;

    bridge::ITimelineController* m_controller = nullptr;
    QDoubleSpinBox* m_bpmSpin = nullptr;

    uint32_t bar_ = 1;
    uint32_t beat_ = 1;
    uint32_t tick_ = 0;
    double seconds_ = 0.0;
    double bpm_ = 120.0;
    bool showBBT_ = true;

    bool draggingBpm_ = false;
    QPointF dragStartPos_;
    double startBpm_ = 120.0;
};

} // namespace presentation::views
