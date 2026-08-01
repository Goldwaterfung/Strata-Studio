// src/Presentation/views/playlist/PlaylistMenuButton.h
#pragma once

#include <QPushButton>
#include <QMenu>
#include "timeline/itimeline_controller.h"
#include "project/iworkspace_controller.h"
#include "engine/iinput_mode_controller.h"
#include "PlaylistEditTool.h"
#include "common/system_primitives.h"

namespace presentation::views {

/**
 * @brief Leftmost dropdown menu button inside the PlaylistToolBar.
 * Contains grouped sub-menus for editing tools, snapping, selection, zoom, transport,
 * and workspace actions (detaching, performance mode).
 */
class PlaylistMenuButton : public QPushButton {
    Q_OBJECT
public:
    explicit PlaylistMenuButton(
        bridge::ITimelineController*  timeline,
        bridge::IWorkspaceController* workspace = nullptr,
        QWidget* parent = nullptr);
    ~PlaylistMenuButton() override = default;


signals:
    void addMarkerRequested();
    void snapSelected(bridge::SnapMode mode);
    void zoomInRequested();
    void zoomOutRequested();
    void zoomResetRequested();
    void undoRequested();
    void redoRequested();
    void selectAllRequested();
    void deselectAllRequested();
    void analyzeLoudnessRequested();
    void saveProjectRequested();
    void saveProjectAsRequested();
    void openProjectRequested();
    void exportProjectJsonRequested();
    void importProjectJsonRequested();


private slots:
    void onMenuTriggered(QAction* action);

private:
    void buildMenu();

private:
    bridge::ITimelineController*  m_timeline{nullptr};
    bridge::IWorkspaceController* m_workspace{nullptr};
    QMenu*                        m_menu{nullptr};

};

} // namespace presentation::views
