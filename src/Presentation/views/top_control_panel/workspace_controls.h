// src/Presentation/views/top_control_panel/workspace_controls.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include "Middle Bridge/project/iworkspace_controller.h"

namespace presentation::views {

/**
 * @brief Window and layout management toggle buttons.
 *
 * Five checkable buttons for Arrangement, Piano Roll, Channel Rack, Mixer, and Browser.
 * Routes through IWorkspaceController.
 */
class WorkspaceControls : public QWidget {
    Q_OBJECT

public:
    explicit WorkspaceControls(QWidget* parent = nullptr);
    ~WorkspaceControls() override = default;

    void bind(bridge::IWorkspaceController* controller);
    void updateFromBridge();

Q_SIGNALS:
    void settingsClicked();
    void windowToggleClicked(bridge::WorkspaceWindow window);
    void analyzeLoudnessClicked();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    QPushButton* createWindowToggle(const QIcon& icon, const QString& text, const QString& tooltip,
                                     bridge::WorkspaceWindow window);

    bridge::IWorkspaceController* m_controller = nullptr;

    QPushButton* m_pianoRollBtn = nullptr;
    QPushButton* m_analyzeLoudnessBtn = nullptr;
    QPushButton* m_mixerBtn = nullptr;
    QPushButton* m_browserBtn = nullptr;
};

} // namespace presentation::views
