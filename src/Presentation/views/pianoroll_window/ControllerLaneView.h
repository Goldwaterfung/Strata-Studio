// src/Presentation/views/pianoroll_window/ControllerLaneView.h
#pragma once

#include <QWidget>
#include "Middle Bridge/midi/imidi_editor_controller.h"

namespace presentation::views {

class ControllerLaneView : public QWidget {
    Q_OBJECT

public:
    explicit ControllerLaneView(bridge::IMidiEditorController* controller, QWidget* parent = nullptr);
    ~ControllerLaneView() override = default;

    void setViewportRange(uint64_t startFrame, uint64_t endFrame);
    void setControllerNumber(uint8_t controllerNumber);

signals:
    void ccMutated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    double frameToX(uint64_t frame) const;
    uint64_t xToFrame(double x) const;

    bridge::IMidiEditorController* controller_ = nullptr;

    uint64_t viewStartFrame_ = 0;
    uint64_t viewEndFrame_ = 44100 * 4;
    uint8_t controllerNumber_ = 1; // Default: Mod Wheel

    static constexpr double KEY_WIDTH = 48.0;
    static constexpr uint32_t MAX_POINTS_BUFFER = 512;

    bool isDrawing_ = false;
};

} // namespace presentation::views
