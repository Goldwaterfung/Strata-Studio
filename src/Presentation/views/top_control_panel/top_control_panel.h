// src/Presentation/views/top_control_panel/top_control_panel.h
#pragma once

#include <QWidget>

#include "Middle Bridge/project/iworkspace_controller.h"

namespace bridge {
class ITimelineController;
class IInputModeController;
}

namespace presentation::views {

class TransportControls;
class InputModeControls;
class WorkspaceControls;

/**
 * @brief Main composite toolbar widget for the top control panel.
 *
 * Layout (horizontal):
 *  ┌──────────────────┬────────────────────┬───────────────────┐
 *  │ TransportControls │ InputModeControls  │ WorkspaceControls │
 *  │ (play/stop/rec,  │ (metro, snap, etc.)│ (window toggles)  │
 *  │  BPM, time)       │                    │                   │
 *  └──────────────────┴────────────────────┴───────────────────┘
 *
 * All sub-widgets communicate exclusively through Middle Bridge interfaces.
 */
class TopControlPanel : public QWidget {
    Q_OBJECT

public:
    explicit TopControlPanel(QWidget* parent = nullptr);
    ~TopControlPanel() override = default;

    /**
     * @brief Bind all sub-widgets to their Middle Bridge controllers.
     *        Must be called once before the panel is shown.
     */
    void bind(bridge::ITimelineController* timelineCtrl,
              bridge::IInputModeController* inputCtrl,
              bridge::IWorkspaceController* workspaceCtrl);

    /**
     * @brief Refresh all sub-widgets from the latest bridge state.
     *        Called by the PresentationDirector at 60 Hz.
     */
    void updateFromBridge();

Q_SIGNALS:
    void settingsClicked();
    void windowToggleClicked(bridge::WorkspaceWindow window);
    void analyzeLoudnessClicked();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    TransportControls* m_transportControls = nullptr;
    InputModeControls* m_inputModeControls = nullptr;
    WorkspaceControls* m_workspaceControls = nullptr;
};

} // namespace presentation::views
