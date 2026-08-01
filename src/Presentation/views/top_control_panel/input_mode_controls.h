// src/Presentation/views/top_control_panel/input_mode_controls.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QMenu>
#include "Middle Bridge/timeline/itimeline_controller.h"
#include "Middle Bridge/engine/iinput_mode_controller.h"

namespace presentation::views {

/**
 * @brief Global recording and input modifier controls.
 *
 * Includes metronome, count-in, typing keyboard, snap mode,
 * loop record mode, and automation link toggles.
 * Routes through ITimelineController and IInputModeController.
 */
class InputModeControls : public QWidget {
    Q_OBJECT

public:
    explicit InputModeControls(QWidget* parent = nullptr);
    ~InputModeControls() override = default;

    void bind(bridge::ITimelineController* timelineCtrl,
              bridge::IInputModeController* inputCtrl);
    void updateFromBridge();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    QPushButton* createCheckableButton(const QIcon& icon, const QString& text, const QString& tooltip);
    QPushButton* createDropdownButton(const QString& text, const QString& tooltip);

    bridge::ITimelineController* m_timelineCtrl = nullptr;
    bridge::IInputModeController* m_inputCtrl = nullptr;

    // Checkable toggles
    QPushButton* m_metronomeBtn = nullptr;

    // Dropdowns
    QPushButton* m_snapBtn = nullptr;
    QMenu* m_snapMenu = nullptr;
};

} // namespace presentation::views
