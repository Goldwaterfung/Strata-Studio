// src/Presentation/views/playlist/PlaylistMenuButton.cpp
#include "PlaylistMenuButton.h"
#include <QMessageBox>
#include <QDebug>
#include "theme.h"
#include <QActionGroup>

namespace presentation::views {

PlaylistMenuButton::PlaylistMenuButton(
    bridge::ITimelineController*  timeline,
    bridge::IWorkspaceController* workspace,
    QWidget* parent)
    : QPushButton(parent)
    , m_timeline(timeline)
    , m_workspace(workspace)
{
    setObjectName("playlistMenuBtn");
    setIcon(theme::PaintHelper::createSvgIcon(":/icons/playlist-menu.svg", QSize(20, 20)));
    setFixedWidth(32);
    setFixedHeight(32);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(
        "QPushButton#playlistMenuBtn::menu-indicator { image: none; }"
    );

    m_menu = new QMenu(this);
    buildMenu();
    setMenu(m_menu);

    connect(m_menu, &QMenu::triggered, this, &PlaylistMenuButton::onMenuTriggered);
}

void PlaylistMenuButton::buildMenu()
{
    m_menu->clear();

    // --- 1. Project Submenu ---
    auto* projectMenu = m_menu->addMenu(QStringLiteral("Project"));
    
    QAction* openAct = projectMenu->addAction(QStringLiteral("Open Session..."));
    openAct->setData(QStringLiteral("open_project"));

    QAction* saveAct = projectMenu->addAction(QStringLiteral("Save Session"));
    saveAct->setData(QStringLiteral("save_project"));

    QAction* saveAsAct = projectMenu->addAction(QStringLiteral("Save Session As..."));
    saveAsAct->setData(QStringLiteral("save_project_as"));

    projectMenu->addSeparator();

    QAction* exportJsonAct = projectMenu->addAction(QStringLiteral("Export Session to JSON..."));
    exportJsonAct->setData(QStringLiteral("export_project_json"));

    QAction* importJsonAct = projectMenu->addAction(QStringLiteral("Import Session from JSON..."));
    importJsonAct->setData(QStringLiteral("import_project_json"));

    m_menu->addSeparator();

    // --- 2. Edit Submenu ---
    auto* editMenu = m_menu->addMenu(QStringLiteral("Edit"));
    
    QAction* undoAct = editMenu->addAction(QStringLiteral("Undo"));
    undoAct->setData(QStringLiteral("undo"));

    QAction* redoAct = editMenu->addAction(QStringLiteral("Redo"));
    redoAct->setData(QStringLiteral("redo"));



    // --- 4. Snap Submenu ---
    auto* snapMenu = m_menu->addMenu(QStringLiteral("Snap"));
    
    struct SnapItem {
        QString label;
        bridge::SnapMode mode;
    };
    const SnapItem snapItems[] = {
        { QStringLiteral("Free (No Snap)"), bridge::SnapMode::Free },
        { QStringLiteral("Bar"), bridge::SnapMode::Bar },
        { QStringLiteral("1/2 Note"), bridge::SnapMode::Note_1_2 },
        { QStringLiteral("1/4 Note"), bridge::SnapMode::Note_1_4 },
        { QStringLiteral("1/8 Note"), bridge::SnapMode::Note_1_8 },
        { QStringLiteral("1/16 Note"), bridge::SnapMode::Note_1_16 },
        { QStringLiteral("1/32 Note"), bridge::SnapMode::Note_1_32 },
        { QStringLiteral("1/64 Note"), bridge::SnapMode::Note_1_64 },
        { QStringLiteral("1/2T Note"), bridge::SnapMode::Note_1_2_Triplet },
        { QStringLiteral("1/4T Note"), bridge::SnapMode::Note_1_4_Triplet },
        { QStringLiteral("1/8T Note"), bridge::SnapMode::Note_1_8_Triplet },
        { QStringLiteral("1/16T Note"), bridge::SnapMode::Note_1_16_Triplet },
        { QStringLiteral("1/32T Note"), bridge::SnapMode::Note_1_32_Triplet },
        { QStringLiteral("1/64T Note"), bridge::SnapMode::Note_1_64_Triplet }
    };
    for (const auto& item : snapItems) {
        QAction* act = snapMenu->addAction(item.label);
        act->setData(QStringLiteral("snap_%1").arg(static_cast<int>(item.mode)));
    }

    m_menu->addSeparator();

    // --- 5. Select Submenu ---
    auto* selectMenu = m_menu->addMenu(QStringLiteral("Select"));
    
    QAction* selAll = selectMenu->addAction(QStringLiteral("Select All"));
    selAll->setData(QStringLiteral("select_all"));
    QAction* deselAll = selectMenu->addAction(QStringLiteral("Deselect All"));
    deselAll->setData(QStringLiteral("deselect_all"));

    m_menu->addSeparator();

    // --- 7. Zoom Submenu ---
    auto* zoomMenu = m_menu->addMenu(QStringLiteral("Zoom"));
    
    QAction* zoomIn = zoomMenu->addAction(QStringLiteral("Zoom In"));
    zoomIn->setData(QStringLiteral("zoom_in"));
    QAction* zoomOut = zoomMenu->addAction(QStringLiteral("Zoom Out"));
    zoomOut->setData(QStringLiteral("zoom_out"));
    QAction* zoomReset = zoomMenu->addAction(QStringLiteral("Reset Zoom"));
    zoomReset->setData(QStringLiteral("zoom_reset"));

    // --- 8. Time Marker Submenu ---
    auto* markerMenu = m_menu->addMenu(QStringLiteral("Time Marker"));
    
    QAction* addMarkerAct = markerMenu->addAction(QStringLiteral("Add Song Marker"));
    addMarkerAct->setData(QStringLiteral("add_marker"));

    m_menu->addSeparator();

    // --- 9. Playhead Submenu ---
    auto* playheadMenu = m_menu->addMenu(QStringLiteral("Playhead"));
    
    QAction* seekStart = playheadMenu->addAction(QStringLiteral("Seek to Song Start"));
    seekStart->setData(QStringLiteral("seek_start"));
    QAction* seekLoop = playheadMenu->addAction(QStringLiteral("Seek to Loop Start"));
    seekLoop->setData(QStringLiteral("seek_loop"));

    m_menu->addSeparator();

    // --- 10. Diagnostics Submenu ---
    auto* diagnosticsMenu = m_menu->addMenu(QStringLiteral("Diagnostics"));
    QAction* analyzeLoudnessAct = diagnosticsMenu->addAction(QStringLiteral("Analyze Session Loudness"));
    analyzeLoudnessAct->setData(QStringLiteral("analyze_loudness"));
}

void PlaylistMenuButton::onMenuTriggered(QAction* action)
{
    if (!action) return;
    const QString id = action->data().toString();

    if (id == QStringLiteral("open_project")) {
        emit openProjectRequested();
    } else if (id == QStringLiteral("save_project")) {
        emit saveProjectRequested();
    } else if (id == QStringLiteral("save_project_as")) {
        emit saveProjectAsRequested();
    } else if (id == QStringLiteral("export_project_json")) {
        emit exportProjectJsonRequested();
    } else if (id == QStringLiteral("import_project_json")) {
        emit importProjectJsonRequested();
    } else if (id == QStringLiteral("undo")) {
        emit undoRequested();
    } else if (id == QStringLiteral("redo")) {
        emit redoRequested();

    } else if (id.startsWith(QStringLiteral("snap_"))) {
        int snapId = id.mid(5).toInt();
        emit snapSelected(static_cast<bridge::SnapMode>(snapId));
    } else if (id == QStringLiteral("select_all")) {
        emit selectAllRequested();
    } else if (id == QStringLiteral("deselect_all")) {
        emit deselectAllRequested();
    } else if (id == QStringLiteral("zoom_in")) {
        emit zoomInRequested();
    } else if (id == QStringLiteral("zoom_out")) {
        emit zoomOutRequested();
    } else if (id == QStringLiteral("zoom_reset")) {
        emit zoomResetRequested();
    } else if (id == QStringLiteral("add_marker")) {
        emit addMarkerRequested();
    } else if (id == QStringLiteral("seek_start")) {
        if (m_timeline) m_timeline->seekToFrame(0);
    } else if (id == QStringLiteral("seek_loop")) {
        if (m_timeline) {
            uint64_t loopStart = m_timeline->getLoopStart();
            m_timeline->seekToFrame(loopStart);
        }
    } else if (id == QStringLiteral("analyze_loudness")) {
        emit analyzeLoudnessRequested();
    }
}

} // namespace presentation::views
