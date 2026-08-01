// src/Presentation/views/mixer/input_slot_widget.h
#pragma once

#include <QWidget>
#include <QString>
#include "Middle Bridge/tracks/itrack_controller.h"

namespace presentation::views {

class InputSlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit InputSlotWidget(QWidget* parent = nullptr);
    ~InputSlotWidget() override = default;

    void bind(bridge::ITrackController* controller, TrackID trackId);
    void updateFromState(const bridge::TrackInputUIState& state);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void showInputMenu(const QPoint& globalPos);

    bridge::ITrackController* m_controller = nullptr;
    TrackID                   m_trackId{};

    QString                   m_inputName{ "None" };
};

using AudioInputSlotWidget = InputSlotWidget;

} // namespace presentation::views
