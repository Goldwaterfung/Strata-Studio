#include "PlaylistWindow.h"
#include <project_config.h>

// Sub-widget includes — forward declarations in header become full types here.
#include "PlaylistMenuButton.h"
#include "PlaylistEditTool.h"
#include "PlaylistTimelineRuler.h"    // Phase 4
#include "TrackHeaderView.h"          // Phase 3
#include "PlaylistClipCanvas.h"         // Phase 2
#include <QCoreApplication>
#include <QEvent>
#include "Middle Bridge/automation/automation_helpers.h"
#include "PickerPanel.h"              // Phase 7
#include "dialogs/MergeArrangementDialog.h"  // Phase 8
#include "dialogs/RenderSettingsDialog.h"     // Phase 8
#include "dialogs/ParameterWindow.h"
#include "MiniPlaylistPreview.h"
#include "ProjectPickerScreen.h"
#include "TempoTrackHeader.h"
#include "TempoTrackCanvas.h"

#include "../theme.h"
#include "../mixer/plugin_editor_dialog.h"
#include "../shortcuts/ShortcutManager.h"
#include <QFileDialog>

#include <algorithm>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollBar>
#include <QShortcut>
#include <QKeySequence>
#include <QLineEdit>
#include <QTextEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRandomGenerator>
#include <QColor>
#include "dialogs/DAWInputDialog.h"

namespace presentation::views {

static uint32_t generateRandomTrackColor() {
    int h = QRandomGenerator::global()->bounded(360);
    int s = 180 + QRandomGenerator::global()->bounded(76); // 180-255 saturation
    int l = 120 + QRandomGenerator::global()->bounded(61); // 120-180 lightness
    return QColor::fromHsl(h, s, l).rgba();
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

PlaylistWindow::PlaylistWindow(const Controllers& ctrl, QWidget* parent)
    : QWidget(parent)
    , m_ctrl(ctrl)
{
    // Register with session manager FIRST so teardown callbacks are wired
    // before any sub-widget construction reads session state.
    if (m_ctrl.sessionManager) {
        m_ctrl.sessionManager->registerChangeListener(this);
    }

    if (m_ctrl.timeline) {
        m_lastBpm = m_ctrl.timeline->getBPM();
    }

    setupUI();
    wireSignals();
    installKeyboardShortcuts();

    // Start the 60 Hz render loop. If no session is active yet, the tick
    // will simply poll stale/empty state — safe because all bridge queries
    // guard against null session internally.
    startRenderLoop();
}

PlaylistWindow::~PlaylistWindow()
{
    // Session teardown law: stop timer before deregistering.
    stopRenderLoop();

    if (m_ctrl.sessionManager) {
        m_ctrl.sessionManager->unregisterChangeListener(this);
    }
}

// ---------------------------------------------------------------------------
// ISessionChangeListener
// ---------------------------------------------------------------------------

void PlaylistWindow::onSessionChanging()
{
    // 1. Stop the 60 Hz tick immediately — no bridge polls during teardown.
    stopRenderLoop();

    // 2. Clear cached widget state.
    //    (Sub-widget clearAll() calls will be un-guarded once phases 2–3 ship.)
    m_trackCount = 0;
    m_lastPlayheadFrame = 0;

    // Phase 3 — TrackHeaderView::clearAll()
    if (m_trackHeader) m_trackHeader->clearAll();

    // Phase 2 — PlaylistClipCanvas::clearAll()
    if (m_canvas) m_canvas->clearAll();
}

void PlaylistWindow::onSessionChanged(composition::IProjectSession* /*newSession*/)
{
    // 1. Reload track list from the fresh session.
    reloadTracks();

    // 2. Restart the 60 Hz render loop.
    startRenderLoop();

    // 3. Refresh timeline cached values (markers, loops)
    if (m_ruler) {
        m_ruler->refreshTimelineCache();
    }
    if (m_tempoCanvas) {
        m_tempoCanvas->refreshTimelineCache();
    }


}

// ---------------------------------------------------------------------------
// paintEvent
// ---------------------------------------------------------------------------

void PlaylistWindow::paintEvent(QPaintEvent* event)
{
    // PlaylistWindow itself is a pure container widget; sub-widgets handle
    // their own painting. We only need to fill the background so there are
    // no transparent gaps between splitter panes.
    QPainter p(this);
    p.fillRect(event->rect(), theme::Color::BgBase);
}

// ---------------------------------------------------------------------------
// Setup Helpers
// ---------------------------------------------------------------------------

void PlaylistWindow::setupUI()
{
    setObjectName("PlaylistWindow");
    setMinimumSize(640, 300);

    // ---- Root vertical layout -----------------------------------------------
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- Horizontal splitter: PickerPanel | Center Pane ---------------------
    auto* outerSplitter = new QSplitter(Qt::Horizontal, this);
    outerSplitter->setHandleWidth(3);
    outerSplitter->setStyleSheet(
        "QSplitter::handle { background-color: #526D82; }"
        "QSplitter::handle:hover { background-color: #00FFCC; }"
    );

    // Phase 7: PickerPanel (hidden by default, toggled via Alt+P)
    m_pickerPanel = new PickerPanel(m_ctrl.browser, m_ctrl.patternData, outerSplitter);
    m_pickerPanel->setVisible(false);
    outerSplitter->addWidget(m_pickerPanel);

    // ---- Centre pane: header row + inner splitter ---------------------------
    auto* centerPane = new QWidget(outerSplitter);
    auto* centerLayout = new QVBoxLayout(centerPane);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    // Header row: [HeaderControlPanel | RulerContainer]
    auto* headerRow = new QWidget(centerPane);
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);
    headerRow->setFixedHeight(40);

    // 1. Consolidated Control Panel (replaces empty headerSpacer)
    auto* headerControl = new QWidget(headerRow);
    headerControl->setFixedWidth(280);
    headerControl->setObjectName("PlaylistHeaderControlPanel");
    headerControl->setStyleSheet(QString(
        "QWidget#PlaylistHeaderControlPanel { background-color: %1; border-bottom: 1px solid #333; }"
    ).arg(theme::Color::BgSurface.name()));

    auto* controlLayout = new QHBoxLayout(headerControl);
    controlLayout->setContentsMargins(8, 5, 8, 5);
    controlLayout->setSpacing(8);

    // PLAYLIST Dropdown Menu Button
    m_menuBtn = new PlaylistMenuButton(m_ctrl.timeline, m_ctrl.workspace, headerControl);
    controlLayout->addWidget(m_menuBtn);

    // ARRANGEMENTS Dropdown Menu Button
    m_arrangementsBtn = new QPushButton("ARRANGEMENTS", headerControl);
    m_arrangementsBtn->setObjectName("arrangementsBtn");
    m_arrangementsBtn->setFont(theme::Font::primary(8, QFont::Bold));
    m_arrangementsBtn->setFixedHeight(30);
    m_arrangementsBtn->setCursor(Qt::PointingHandCursor);
    m_arrangementsBtn->setStyleSheet(
        "QPushButton#arrangementsBtn { background-color: #323232; color: #E8E8E8; border: 1px solid #444; border-radius: 4px; padding: 0 8px; }"
        "QPushButton#arrangementsBtn:hover { background-color: #424242; }"
        "QPushButton#arrangementsBtn::menu-indicator { image: none; }"
    );
    m_arrangementsMenu = new QMenu(this);
    m_arrangementsBtn->setMenu(m_arrangementsMenu);
    controlLayout->addWidget(m_arrangementsBtn);

    headerLayout->addWidget(headerControl);

    // 2. Timeline Ruler & FPS Label Container
    auto* rulerContainer = new QWidget(headerRow);
    auto* rulerLayout = new QHBoxLayout(rulerContainer);
    rulerLayout->setContentsMargins(0, 0, 0, 0);
    rulerLayout->setSpacing(4);

    // Phase 4: PlaylistTimelineRuler (expands, synced x-scroll with canvas)
    m_ruler = new PlaylistTimelineRuler(m_ctrl.timeline, m_ctrl.inputMode, m_ctrl.arrangement, rulerContainer);
    rulerLayout->addWidget(m_ruler, 1);

    m_fpsLabel = new QLabel("FPS: 0", rulerContainer);
    m_fpsLabel->setFont(theme::Font::monospace(9, QFont::Bold));
    m_fpsLabel->setStyleSheet(QString("color: %1; background: transparent; padding-right: 8px;").arg(theme::Color::AccentRecord.name()));
    rulerLayout->addWidget(m_fpsLabel);

    headerLayout->addWidget(rulerContainer, 1);

    centerLayout->addWidget(headerRow);

    // Tempo automation track row: [TempoTrackHeader | TempoTrackCanvas]
    m_tempoRow = new QWidget(centerPane);
    auto* tempoLayout = new QHBoxLayout(m_tempoRow);
    tempoLayout->setContentsMargins(0, 0, 0, 0);
    tempoLayout->setSpacing(0);
    m_tempoRow->setFixedHeight(12); // Collapsed by default (12px)

    m_tempoHeader = new TempoTrackHeader(m_ctrl.timeline, m_tempoRow);
    m_tempoHeader->setCollapsed(true);
    m_tempoCanvas = new TempoTrackCanvas(m_ctrl.timeline, m_ctrl.inputMode, m_ctrl.arrangement, m_tempoRow);

    tempoLayout->addWidget(m_tempoHeader);
    tempoLayout->addWidget(m_tempoCanvas, 1);

    centerLayout->addWidget(m_tempoRow);

    // Inner splitter: [TrackHeaderView | PlaylistClipCanvas]
    auto* innerSplitter = new QSplitter(Qt::Horizontal, centerPane);
    innerSplitter->setHandleWidth(1);
    innerSplitter->setChildrenCollapsible(false);
    innerSplitter->setStyleSheet(
        "QSplitter::handle { background-color: #526D82; }"
    );

    // Phase 3: TrackHeaderView (fixed width ~280 px)
    m_trackHeader = new TrackHeaderView(m_ctrl.track, m_ctrl.metering, m_ctrl.arrangement, m_ctrl.automation, innerSplitter);
    m_trackHeader->setFixedWidth(280);
    innerSplitter->addWidget(m_trackHeader);

    // Phase 2: PlaylistClipCanvas (main expanding grid)
    m_canvas = new PlaylistClipCanvas(m_ctrl.arrangement, m_ctrl.timeline,
                                      m_ctrl.waveform, m_ctrl.automation,
                                      m_ctrl.patternData, m_ctrl.browser,
                                      m_ctrl.inputMode, innerSplitter);
    innerSplitter->addWidget(m_canvas);

    // Lock the header view at 280 px; canvas gets remaining space
    innerSplitter->setStretchFactor(0, 0);
    innerSplitter->setStretchFactor(1, 1);

    centerLayout->addWidget(innerSplitter, 1);

    // Phase 10 — MiniPlaylistPreview Birds-Eye navigator bar
    m_preview = new MiniPlaylistPreview(m_ctrl.arrangement, m_ctrl.timeline, centerPane);
    centerLayout->addWidget(m_preview);

    outerSplitter->addWidget(centerPane);
    outerSplitter->setStretchFactor(0, 0);  // PickerPanel: fixed
    outerSplitter->setStretchFactor(1, 1);  // CenterPane: expanding

    root->addWidget(outerSplitter, 1);

    // Phase 11 — Fullscreen dashboard picker overlay floats on top of root window
    m_projectPicker = new ProjectPickerScreen(m_ctrl.lifecycle, m_ctrl.browser, this);

    // Setup FPS monitoring relocated from PlaylistToolBar
    qApp->installEventFilter(this);
    m_fpsTimer.start();
    connect(&m_fpsUpdateTimer, &QTimer::timeout, this, [this]() {
        qint64 elapsed = m_fpsTimer.elapsed();
        if (elapsed > 0) {
            int fps = static_cast<int>((m_frameCount * 1000.0) / elapsed);
            if (m_fpsLabel) {
                m_fpsLabel->setText(QString("FPS: %1").arg(fps));
            }
        }
        m_frameCount = 0;
        m_fpsTimer.restart();
    });
    m_fpsUpdateTimer.start(1000);
}

void PlaylistWindow::wireSignals()
{
    // Arrangements Menu Setup
    if (m_arrangementsMenu) {
        connect(m_arrangementsMenu, &QMenu::aboutToShow, this, &PlaylistWindow::buildArrangementsMenu);
        connect(m_arrangementsMenu, &QMenu::triggered, this, &PlaylistWindow::onArrangementsMenuTriggered);
    }

    // Playlist Menu Button signals:
    if (m_menuBtn) {
        connect(m_menuBtn, &PlaylistMenuButton::zoomInRequested,
                this, [this]() { zoomHorizontal(1.25); });
        connect(m_menuBtn, &PlaylistMenuButton::zoomOutRequested,
                this, [this]() { zoomHorizontal(0.8); });
        connect(m_menuBtn, &PlaylistMenuButton::zoomResetRequested,
                this, &PlaylistWindow::zoomReset);
        connect(m_menuBtn, &PlaylistMenuButton::addMarkerRequested, this, [this]() {
            if (m_ctrl.timeline) {
                bool ok = false;
                QString text = DAWInputDialog::getText(this, QStringLiteral("Add Song Marker"),
                                                     QStringLiteral("Marker Name:"),
                                                     QStringLiteral("Marker"), &ok);
                if (ok && !text.isEmpty()) {
                    uint64_t currentFrame = m_ctrl.timeline->getCurrentFrame();
                    m_ctrl.timeline->addMarker(currentFrame, text.toUtf8().constData(), 0xFF00FFCC);
                    if (m_ruler) m_ruler->refreshTimelineCache();
                }
            }
        });
        connect(m_menuBtn, &PlaylistMenuButton::undoRequested, this, [this]() {
            if (m_ctrl.arrangement && m_ctrl.arrangement->undo()) {
                reloadTracks();
            }
        });
        connect(m_menuBtn, &PlaylistMenuButton::redoRequested, this, [this]() {
            if (m_ctrl.arrangement && m_ctrl.arrangement->redo()) {
                reloadTracks();
            }
        });
        connect(m_menuBtn, &PlaylistMenuButton::selectAllRequested, this, [this]() {
            if (m_canvas) m_canvas->selectAll();
        });
        connect(m_menuBtn, &PlaylistMenuButton::deselectAllRequested, this, [this]() {
            if (m_canvas) m_canvas->deselectAll();
        });
        connect(m_menuBtn, &PlaylistMenuButton::analyzeLoudnessRequested, this, &PlaylistWindow::onAnalyzeLoudnessRequested);
        connect(m_menuBtn, &PlaylistMenuButton::saveProjectRequested, this, &PlaylistWindow::onSaveProject);
        connect(m_menuBtn, &PlaylistMenuButton::saveProjectAsRequested, this, &PlaylistWindow::onSaveProjectAs);
        connect(m_menuBtn, &PlaylistMenuButton::openProjectRequested, this, &PlaylistWindow::onOpenProject);
        connect(m_menuBtn, &PlaylistMenuButton::exportProjectJsonRequested, this, &PlaylistWindow::onExportProjectJson);
        connect(m_menuBtn, &PlaylistMenuButton::importProjectJsonRequested, this, &PlaylistWindow::onImportProjectJson);
        connect(m_menuBtn, &PlaylistMenuButton::snapSelected, this, [this](bridge::SnapMode mode) {
            if (m_ctrl.inputMode) {
                m_ctrl.inputMode->setSnapMode(mode);
            }
        });
    }



    // Phase 3 — TrackHeaderView signals:
    connect(m_trackHeader, &TrackHeaderView::addAudioTrackRequested,
            this, &PlaylistWindow::onAddAudioTrackRequested);
    connect(m_trackHeader, &TrackHeaderView::addInstrumentTrackRequested,
            this, &PlaylistWindow::onAddInstrumentTrackRequested);
    connect(m_trackHeader, &TrackHeaderView::addFolderTrackRequested, this, [this]() {
        if (m_ctrl.track) {
            m_ctrl.track->addFolderTrack("Folder", generateRandomTrackColor());
        }
    });
    connect(m_trackHeader, &TrackHeaderView::trackMuteToggled,
            this, &PlaylistWindow::onTrackMuteToggled);
    connect(m_trackHeader, &TrackHeaderView::trackSoloToggled,
            this, &PlaylistWindow::onTrackSoloToggled);
    connect(m_trackHeader, &TrackHeaderView::trackArmToggled,
            this, &PlaylistWindow::onTrackArmToggled);
    connect(m_trackHeader, &TrackHeaderView::trackInputMonitorToggled,
            this, &PlaylistWindow::onTrackInputMonitorToggled);
    connect(m_trackHeader, &TrackHeaderView::trackRenameRequested,
            this, &PlaylistWindow::onTrackRenameRequested);
    connect(m_trackHeader, &TrackHeaderView::trackColorChangeRequested,
            this, &PlaylistWindow::onTrackColorChangeRequested);
    connect(m_trackHeader, &TrackHeaderView::trackMoveRequested,
            this, &PlaylistWindow::onTrackMoveRequested);
    connect(m_trackHeader, &TrackHeaderView::trackDeleteRequested,
            this, &PlaylistWindow::onTrackDeleteRequested);
    connect(m_trackHeader, &TrackHeaderView::insertPluginRequested,
            this, &PlaylistWindow::onInsertPluginRequested);
    connect(m_trackHeader, &TrackHeaderView::insertInstrumentRequested,
            this, &PlaylistWindow::onInsertInstrumentRequested);
    connect(m_trackHeader, &TrackHeaderView::trackHeightChanged, this, [this](TrackID id, int newHeight) {
        m_trackHeights[id.toRaw()] = newHeight;
        applyViewport();
    });
    // Flaw 1 fix: immediately reload track state when the user clicks the ▼/▲ Auto
    // expand button. Without this, the expanded flag is written to the bridge but
    // TrackHeaderView and PlaylistClipCanvas do not receive updated TrackUIState
    // until the next 60 Hz tick — causing the sub-lane to appear with one frame of
    // lag and getRegionsInViewport to miss the new height layout.
    connect(m_trackHeader, &TrackHeaderView::automationExpansionToggled,
            this, [this](TrackID /*id*/) {
        reloadTracks();
    });
    connect(m_trackHeader, &TrackHeaderView::takesExpansionToggled,
            this, [this](TrackID /*id*/) {
        reloadTracks();
    });
    connect(m_trackHeader, &TrackHeaderView::configureAutomationRequested,
            this, &PlaylistWindow::onConfigureAutomationRequested);

    // Live preview during automation sub-lane drag (lightweight, no reload)
    connect(m_trackHeader, &TrackHeaderView::automationSubLaneHeightChanging,
            this, [this](TrackID trackId, uint32_t subLaneIndex, int newHeight) {
        m_autoSubLaneHeights[autoSubLaneKey(trackId, subLaneIndex)] =
            static_cast<uint32_t>(newHeight);
        applyViewport();
    });

    // Commit automation sub-lane height on release → persist to bridge + reload
    connect(m_trackHeader, &TrackHeaderView::automationSubLaneHeightChanged,
            this, [this](TrackID trackId, uint32_t subLaneIndex, int newHeight) {
        if (m_ctrl.track) {
            m_ctrl.track->setAutomationSubLaneHeight(
                trackId, subLaneIndex, static_cast<uint32_t>(newHeight));
            reloadTracks();
        }
    });

    // Real-time preview for takes lane height
    connect(m_trackHeader, &TrackHeaderView::takesLaneHeightChanging,
            this, [this](TrackID trackId, uint32_t takeLaneIndex, int newHeight) {
        m_takesLaneHeights[{trackId.toRaw(), takeLaneIndex}] = newHeight;
        applyViewport();
    });

    // Commit takes lane height on release
    connect(m_trackHeader, &TrackHeaderView::takesLaneHeightChanged,
            this, [this](TrackID tid, uint32_t laneIdx, int newHeight) {
        m_takesLaneHeights[{tid.toRaw(), laneIdx}] = newHeight;
        reloadTracks();
    });
    
    connect(m_trackHeader, &TrackHeaderView::takePromoteRequested,
            this, [this](TrackID trackId, uint32_t laneIndex) {
        if (m_canvas->hasCompHighlight() && m_canvas->getCompHighlightTrack() == trackId && m_canvas->getCompHighlightLane() == static_cast<int>(laneIndex)) {
            m_ctrl.arrangement->swapTakeLayer(trackId, laneIndex, m_canvas->getCompHighlightStart(), m_canvas->getCompHighlightLength());
            m_canvas->clearCompHighlight();
        } else {
            bool promotedSelected = false;
            auto selectedIds = m_canvas->getSelectedRegions();
            for (uint64_t rawId : selectedIds) {
                bridge::VisualRegion region;
                if (m_ctrl.arrangement->getVisualRegion(bridge::RegionID::fromRaw(rawId), region)) {
                    if (region.trackId == trackId && region.layerIndex == laneIndex) {
                        m_ctrl.arrangement->swapTakeLayer(trackId, laneIndex, region.startFrame, region.durationFrames);
                        promotedSelected = true;
                    }
                }
            }
            if (!promotedSelected) {
                m_ctrl.arrangement->swapTakeLayer(trackId, laneIndex, 0, 0); // Full layer swap
            }
        }
        reloadTracks();
    });

    // Phase 2 — PlaylistClipCanvas signals:
    if (m_canvas) {
        connect(m_canvas, &PlaylistClipCanvas::regionMoveRequested,
                this, &PlaylistWindow::onRegionMoveRequested);
        connect(m_canvas, &PlaylistClipCanvas::regionSplitRequested,
                this, &PlaylistWindow::onRegionSplitRequested);
        connect(m_canvas, &PlaylistClipCanvas::regionDeleteRequested,
                this, &PlaylistWindow::onRegionDeleteRequested);
        connect(m_canvas, &PlaylistClipCanvas::regionGainChanged,
                this, &PlaylistWindow::onRegionGainChanged);
        connect(m_canvas, &PlaylistClipCanvas::regionFadesChanged,
                this, &PlaylistWindow::onRegionFadesChanged);
        connect(m_canvas, &PlaylistClipCanvas::regionTrimRequested, this, [this](bridge::RegionID id, uint64_t newPosition, uint64_t newSourceStart, uint64_t newDuration) {
            if (m_ctrl.arrangement) {
                m_ctrl.arrangement->trimRegion(id, newPosition, newSourceStart, newDuration);
            }
        });
        connect(m_canvas, &PlaylistClipCanvas::regionStretchRequested, this, [this](bridge::RegionID id, double ratio) {
            if (m_ctrl.arrangement) {
                m_ctrl.arrangement->setRegionPlaybackRatio(id, static_cast<float>(ratio));
            }
        });
        connect(m_canvas, &PlaylistClipCanvas::midiClipDoubleClicked,
                this, &PlaylistWindow::midiClipDoubleClicked);
                
        connect(m_canvas, &PlaylistClipCanvas::trackSelectionRequested, this, [this](TrackID trackId, bool multiSelect, bool rangeSelect) {
            if (m_ctrl.track) {
                if (!multiSelect && !rangeSelect) {
                    m_ctrl.track->clearTrackSelection();
                }
                m_ctrl.track->setTrackSelected(trackId, true);
                reloadTracks();
            }
        });

        // Drag and Drop connections
        connect(m_canvas, &PlaylistClipCanvas::addAudioTrackWithClipRequested,
                this, &PlaylistWindow::onAddAudioTrackWithClipRequested);
        connect(m_canvas, &PlaylistClipCanvas::addAudioTracksWithClipsRequested,
                this, &PlaylistWindow::onAddAudioTracksWithClipsRequested);
        connect(m_canvas, &PlaylistClipCanvas::addInstrumentTrackWithPluginRequested,
                this, &PlaylistWindow::onAddInstrumentTrackWithPluginRequested);
        connect(m_canvas, &PlaylistClipCanvas::insertInstrumentRequested,
                this, &PlaylistWindow::onInsertInstrumentRequested);
        connect(m_canvas, &PlaylistClipCanvas::insertPluginRequested,
                this, &PlaylistWindow::onInsertPluginRequested);
    }

    // Phase 4 — PlaylistTimelineRuler signals:
    connect(m_ruler, &PlaylistTimelineRuler::seekRequested,
            this, &PlaylistWindow::onSeekRequested);
    connect(m_ruler, &PlaylistTimelineRuler::zoomScrollChanged,
            this, &PlaylistWindow::onZoomScrollChanged);
    connect(m_ruler, &PlaylistTimelineRuler::loopRangeChanged, this, [this](uint64_t start, uint64_t end) {
        if (m_ctrl.timeline) {
            m_ctrl.timeline->setLoopRange(start, end);
        }
    });

    // Tempo Track signals:
    if (m_tempoHeader) {
        connect(m_tempoHeader, &TempoTrackHeader::collapseToggled,
                this, &PlaylistWindow::onTempoCollapseToggled);
        connect(m_tempoHeader, &TempoTrackHeader::heightResizeRequested,
                this, &PlaylistWindow::onTempoHeightResizeRequested);
    }
    if (m_tempoCanvas) {
        connect(m_tempoCanvas, &TempoTrackCanvas::heightResizeRequested,
                this, &PlaylistWindow::onTempoHeightResizeRequested);
    }

    // Phase 7 — PickerPanel signals:
    if (m_pickerPanel) {
        connect(m_pickerPanel, &PickerPanel::clipDragStarted, this, [this](MediaID mediaId, int itemType) {
            qDebug() << "PickerPanel: Drag started for MediaID raw:" << mediaId.toRaw() << "Type:" << itemType;
            if (m_canvas) {
                m_canvas->setActiveDragType(itemType);
            }
        });
    }

    // Phase 10 — MiniPlaylistPreview seek:
    if (m_preview) {
        connect(m_preview, &MiniPlaylistPreview::viewportSeekRequested, this, [this](uint64_t newStart) {
            uint64_t range = m_viewport.endFrame - m_viewport.startFrame;
            m_viewport.startFrame = newStart;
            m_viewport.endFrame = newStart + range;
            applyViewport();
        });
    }
}

void PlaylistWindow::installKeyboardShortcuts()
{
    // Keyboard shortcuts managed centrally via presentation::shortcuts::ShortcutManager
    auto& sm = presentation::shortcuts::ShortcutManager::instance();
    using presentation::shortcuts::ShortcutAction;

    // Tool selection shortcuts
    sm.bind(this, ShortcutAction::Playlist_ToolDraw,         [this]() { m_activeTool = PlaylistEditTool::Draw; });
    sm.bind(this, ShortcutAction::Playlist_ToolPaint,        [this]() { m_activeTool = PlaylistEditTool::Paint; });
    sm.bind(this, ShortcutAction::Playlist_ToolDelete,       [this]() { m_activeTool = PlaylistEditTool::Delete; });
    sm.bind(this, ShortcutAction::Playlist_ToolMute,         [this]() { m_activeTool = PlaylistEditTool::Mute; });
    sm.bind(this, ShortcutAction::Playlist_ToolSlipEdit,     [this]() { m_activeTool = PlaylistEditTool::SlipEdit; });
    sm.bind(this, ShortcutAction::Playlist_ToolSlice,        [this]() { m_activeTool = PlaylistEditTool::Slice; });
    sm.bind(this, ShortcutAction::Playlist_ToolSelect,       [this]() { m_activeTool = PlaylistEditTool::Select; });
    sm.bind(this, ShortcutAction::Playlist_ToolZoomSelect,   [this]() { m_activeTool = PlaylistEditTool::ZoomSelect; });
    sm.bind(this, ShortcutAction::Playlist_ToolPlaySelected, [this]() { m_activeTool = PlaylistEditTool::PlaySelected; });

    // Toggle Track Automation Lanes (A)
    sm.bind(this, ShortcutAction::Playlist_ToggleAutomation, [this]() {
        if (!m_ctrl.track) return;
        auto tracks = m_ctrl.track->getAllTracks();
        if (tracks.empty()) return;

        bool targetState = false;
        for (const auto& tr : tracks) {
            if (!tr.isAutomationExpanded) {
                targetState = true;
                break;
            }
        }

        for (const auto& tr : tracks) {
            m_ctrl.track->setAutomationExpanded(tr.trackId, targetState);
        }
        reloadTracks();
    });

    // Canvas mode shortcuts
    sm.bindSequence(this, QKeySequence("Shift+F"), [this]() { if (m_canvas) m_canvas->setShowFades(!m_canvas->showFades()); });
    sm.bindSequence(this, QKeySequence("Shift+M"), [this]() { if (m_canvas) m_canvas->setStretchMode(!m_canvas->stretchMode()); });

    // Alt+P: toggle PickerPanel visibility
    sm.bind(this, ShortcutAction::Playlist_OpenProjectPicker, [this]() {
        if (m_pickerPanel) {
            m_pickerPanel->setVisible(!m_pickerPanel->isVisible());
        }
    });

    // Standard Edit & File Shortcuts
    sm.bind(this, ShortcutAction::Playlist_SelectAll, [this]() {
        if (m_canvas) m_canvas->selectAll();
    });
    sm.bind(this, ShortcutAction::Global_SaveProject, [this]() {
        onSaveProject();
    });
    sm.bind(this, ShortcutAction::Global_SaveProjectAs, [this]() {
        onSaveProjectAs();
    });
    sm.bind(this, ShortcutAction::Global_OpenProject, [this]() {
        onOpenProject();
    });
    sm.bind(this, ShortcutAction::Playlist_OpenExportDialog, [this]() {
        RenderSettingsDialog dialog(m_ctrl.render, m_ctrl.timeline, m_ctrl.arrangement, this);
        if (dialog.exec() == QDialog::Accepted) {
            qDebug() << "RenderSettingsDialog: Export finished successfully!";
        }
    });

    // Zoom shortcuts
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_Equal), [this]() { zoomHorizontal(1.25); });
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_Plus), [this]() { zoomHorizontal(1.25); });
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_Minus), [this]() { zoomHorizontal(0.8); });
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_0), [this]() { zoomReset(); });

    // Ctrl+F8 / Cmd+F8: show ProjectPickerScreen overlay
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_F8), [this]() {
        if (m_projectPicker) {
            m_projectPicker->showOverlay();
        }
    });

    // Add Marker (Ctrl+M / Cmd+M)
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_M), [this]() {
        if (m_ctrl.timeline) {
            bool ok = false;
            QString text = DAWInputDialog::getText(this, QStringLiteral("Add Song Marker"),
                                                 QStringLiteral("Marker Name:"),
                                                 QStringLiteral("Marker"), &ok);
            if (ok && !text.isEmpty()) {
                uint64_t frame = m_ctrl.timeline->getCurrentFrame();
                m_ctrl.timeline->addMarker(frame, text.toUtf8().constData(), 0xFF00FFCC);
                if (m_ruler) m_ruler->refreshTimelineCache();
            }
        }
    });

    // Split / Cut Clip at Playhead (Ctrl+E / Cmd+E)
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_E), [this]() {
        if (m_canvas && m_ctrl.timeline) {
            m_canvas->splitClipsAtPlayhead(m_ctrl.timeline->getCurrentFrame());
        }
    });

    // Delete Selected Clips (Delete / Backspace)
    sm.bindSequence(this, QKeySequence(Qt::Key_Delete), [this]() {
        if (m_canvas) m_canvas->deleteSelectedClips();
    });
    sm.bindSequence(this, QKeySequence(Qt::Key_Backspace), [this]() {
        if (m_canvas) m_canvas->deleteSelectedClips();
    });

    // Duplicate Selected Clips (Ctrl+D / Cmd+D)
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_D), [this]() {
        if (m_canvas) m_canvas->duplicateSelectedClips();
    });

    // Mute / Unmute Selected Clips (Alt+M / M)
    sm.bindSequence(this, QKeySequence("Alt+M"), [this]() {
        if (m_canvas) m_canvas->toggleMuteSelectedClips();
    });

    // Quantize Clip Start Times (Ctrl+Q / Cmd+Q)
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_Q), [this]() {
        if (m_canvas) m_canvas->quantizeSelectedClips();
    });

    // Join / Consolidate Clips (Ctrl+J / Cmd+J)
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_J), [this]() {
        if (m_canvas) m_canvas->consolidateSelectedClips();
    });

    // Set Loop Range to Selection (Ctrl+U / Cmd+U)
    sm.bindSequence(this, QKeySequence(Qt::CTRL | Qt::Key_U), [this]() {
        if (m_canvas && m_ctrl.timeline) {
            uint64_t start = 0, end = 0;
            if (m_canvas->getSelectedRange(start, end)) {
                m_ctrl.timeline->setLoopRange(start, end);
                m_ctrl.timeline->setLoopEnabled(true);
            }
        }
    });

    // Nudge Selected Clips (Alt+Left / Alt+Right)
    sm.bindSequence(this, QKeySequence("Alt+Left"), [this]() {
        if (m_canvas && m_ctrl.timeline) {
            uint64_t nudgeFrames = m_ctrl.timeline->ticksToSamples(60); // 1/16 note default
            m_canvas->nudgeSelectedClips(-static_cast<int64_t>(nudgeFrames));
        }
    });
    sm.bindSequence(this, QKeySequence("Alt+Right"), [this]() {
        if (m_canvas && m_ctrl.timeline) {
            uint64_t nudgeFrames = m_ctrl.timeline->ticksToSamples(60);
            m_canvas->nudgeSelectedClips(static_cast<int64_t>(nudgeFrames));
        }
    });

    // Move Clip Between Tracks (Shift+Up / Shift+Down)
    sm.bindSequence(this, QKeySequence("Shift+Up"), [this]() {
        if (m_canvas) m_canvas->moveSelectedClipsTrack(-1);
    });
    sm.bindSequence(this, QKeySequence("Shift+Down"), [this]() {
        if (m_canvas) m_canvas->moveSelectedClipsTrack(1);
    });

    // Insert Audio Clip File (Ctrl+Shift+I)
    sm.bindSequence(this, QKeySequence("Ctrl+Shift+I"), [this]() {
        onImportAudioFile();
    });

    // Insert Blank MIDI Clip (Ctrl+Shift+M)
    sm.bindSequence(this, QKeySequence("Ctrl+Shift+M"), [this]() {
        onInsertBlankMidiClip();
    });

    // Rename Selected Clip (F2)
    sm.bindSequence(this, QKeySequence("F2"), [this]() {
        if (m_canvas) m_canvas->renameSelectedClip();
    });

    // Configure Clip Fades Dialog (Ctrl+Alt+F)
    sm.bindSequence(this, QKeySequence("Ctrl+Alt+F"), [this]() {
        if (m_canvas) m_canvas->openFadeConfigDialog();
    });
}

// ---------------------------------------------------------------------------
// Render Loop
// ---------------------------------------------------------------------------

void PlaylistWindow::startRenderLoop()
{
    if (!m_renderTimer) {
        m_renderTimer = new QTimer(this);
        m_renderTimer->setTimerType(Qt::PreciseTimer);
        connect(m_renderTimer, &QTimer::timeout, this, &PlaylistWindow::onRenderTick);
    }
    m_elapsedTimer.restart();
    m_renderTimer->start(16); // ~60 Hz
}

void PlaylistWindow::stopRenderLoop()
{
    if (m_renderTimer) {
        m_renderTimer->stop();
    }
}

// ---------------------------------------------------------------------------
// Viewport Propagation
// ---------------------------------------------------------------------------

void PlaylistWindow::applyViewport()
{
    // Rebuild track layouts
    if (m_ctrl.track) {
        m_trackLayouts = buildTrackLayouts(m_ctrl.track->getAllTracks(), m_trackHeights, m_autoSubLaneHeights, m_takesLaneHeights, kDefaultTrackHeight);
    } else {
        m_trackLayouts.clear();
    }

    // Clamp the vertical scroll offset to stay within valid tracks bounds
    int maxOffset = getMaxVerticalOffset();
    m_viewport.verticalOffsetPx = std::clamp(m_viewport.verticalOffsetPx, 0, maxOffset);

    // Phase 4 — Ruler:
    if (m_ruler) {
        m_ruler->setViewState(m_viewport.startFrame, m_viewport.endFrame, m_viewport.zoomFactor);
    }

    // Phase 2 — Canvas:
    if (m_canvas) {
        PlaylistClipCanvas::ViewState vs;
        vs.viewStartFrame  = m_viewport.startFrame;
        vs.viewEndFrame    = m_viewport.endFrame;
        vs.zoomFactor      = m_viewport.zoomFactor;
        vs.trackCount      = m_trackCount;
        vs.trackLayouts    = m_trackLayouts;
        vs.defaultTrackHeight = kDefaultTrackHeight;
        vs.verticalOffsetPx   = m_viewport.verticalOffsetPx;
        m_canvas->setViewState(vs);
    }

    // Phase 3 — TrackHeaderView vertical scroll sync:
    if (m_trackHeader) {
        m_trackHeader->setVerticalOffset(m_viewport.verticalOffsetPx);
    }

    // Phase 10 — MiniPlaylistPreview visible bracket:
    if (m_preview) {
        m_preview->setViewport(m_viewport.startFrame, m_viewport.endFrame);
    }

    // Tempo Canvas view state:
    if (m_tempoCanvas) {
        m_tempoCanvas->setViewState(m_viewport.startFrame, m_viewport.endFrame, m_viewport.zoomFactor);
    }
}

// ---------------------------------------------------------------------------
// Track Reload
// ---------------------------------------------------------------------------

void PlaylistWindow::reloadTracks()
{
    if (!m_ctrl.track) {
        m_trackCount = 0;
        return;
    }

    std::vector<bridge::TrackUIState> tracks = m_ctrl.track->getAllTracks();
    m_trackCount = static_cast<uint32_t>(tracks.size());
    m_autoSubLaneHeights.clear();

    // Phase 3 — TrackHeaderView:
    if (m_trackHeader) m_trackHeader->setTrackList(tracks, m_takesLaneHeights);

    // Phase 2 — PlaylistClipCanvas:
    if (m_canvas) m_canvas->setTrackList(tracks);

    applyViewport();

    Q_EMIT tracksChanged(tracks);
}

int PlaylistWindow::getMaxVerticalOffset() const
{
    int totalTracksHeight = 0;
    for (const auto& layout : m_trackLayouts) {
        totalTracksHeight += static_cast<int>(layout.totalHeight);
    }

    // Add track footer height
    int totalContentHeight = totalTracksHeight + TrackHeaderView::kFooterHeight;

    // Viewport visible height
    int viewportHeight = m_canvas ? m_canvas->height() : 0;

    return std::max(0, totalContentHeight - viewportHeight);
}

// ---------------------------------------------------------------------------
// 60 Hz Render Tick
// ---------------------------------------------------------------------------

void PlaylistWindow::onRenderTick()
{
    const double elapsedMs = static_cast<double>(m_elapsedTimer.restart());

    // --- 1. Update metering ballistics (once per tick, GUI thread only) ------
    if (m_ctrl.metering) {
        m_ctrl.metering->updateMeters(elapsedMs);
    }

    // --- 2. Real-Time Track State Sync ---------------------------------------
    if (m_ctrl.track && m_trackHeader) {
        std::vector<bridge::TrackUIState> tracks = m_ctrl.track->getAllTracks();
        
        bool structureChanged = (tracks.size() != m_trackCount);
        if (!structureChanged) {
            const auto& oldTracks = m_trackHeader->getTracks();
            if (tracks.size() == oldTracks.size()) {
                for (size_t i = 0; i < tracks.size(); ++i) {
                    if (tracks[i].trackId != oldTracks[i].trackId ||
                        tracks[i].parentFolderId != oldTracks[i].parentFolderId ||
                        tracks[i].outputTargetTrackId != oldTracks[i].outputTargetTrackId) {
                        structureChanged = true;
                        break;
                    }
                }
            } else {
                structureChanged = true;
            }
        }

        if (structureChanged) {
            // Structural change (add/remove track, routing change) -> Rebuild full view
            reloadTracks();
        } else {
            // Update button/toggle/color/name states in-place
            m_trackHeader->updateTrackStates(tracks, m_takesLaneHeights);
            Q_EMIT trackStatesUpdated(tracks);
        }
    }

    // --- 3. Fetch playhead frame and propagate only on change ----------------
    uint64_t playheadFrame = m_lastPlayheadFrame;
    if (m_ctrl.timeline) {
        playheadFrame = m_ctrl.timeline->getCurrentFrame();
        if (m_ruler) {
            m_ruler->refreshTimelineCache();
            m_ruler->setLoopState(m_ctrl.timeline->isLooping(),
                                  m_ctrl.timeline->getLoopStart(),
                                  m_ctrl.timeline->getLoopEnd());
        }

        // Trigger updates when the BPM changes so timeline coordinates refresh immediately
        double currentBpm = m_ctrl.timeline->getBPM();
        if (std::abs(currentBpm - m_lastBpm) > 0.001) {
            m_lastBpm = currentBpm;
            if (m_ruler) {
                m_ruler->update();
            }
            if (m_canvas) {
                m_canvas->update();
            }
            if (m_tempoCanvas) {
                m_tempoCanvas->update();
            }
        }
    }



    if (m_tempoHeader && m_ctrl.timeline) {
        m_tempoHeader->updateBpmReadout(m_ctrl.timeline->getBPM());
    }

    const bool playheadMoved = (playheadFrame != m_lastPlayheadFrame);
    if (playheadMoved) {
        m_lastPlayheadFrame = playheadFrame;

        // Phase 4:
        if (m_ruler) m_ruler->setPlayheadFrame(playheadFrame);
        // Phase 2:
        if (m_canvas) m_canvas->setPlayheadFrame(playheadFrame);
    }

    // --- 4. Push meter levels to TrackHeaderView -----------------------------
    // Phase 3:
    if (m_trackHeader && m_ctrl.metering && m_trackCount > 0) {
        m_trackHeader->updateMeters();
    }
}

// ---------------------------------------------------------------------------
// Track Mutation Handlers
// ---------------------------------------------------------------------------

void PlaylistWindow::onAddAudioTrackRequested()
{
    if (!m_ctrl.track) return;
    m_ctrl.track->addAudioTrack("Audio", 2, generateRandomTrackColor());
    reloadTracks();
}

void PlaylistWindow::onAddInstrumentTrackRequested()
{
    if (!m_ctrl.track) return;

    QMenu menu(this);
    menu.setStyleSheet(theme::Style::getGlobalStyleSheet());
    menu.setProperty("class", "instrumentMenu");

    std::vector<PluginDescriptor> plugins = m_ctrl.track->getAvailablePlugins();

    bool hasInstruments = false;
    for (const auto& plug : plugins) {
        if (plug.category == PluginCategory::INSTRUMENT) {
            hasInstruments = true;
            QAction* act = menu.addAction(QString::fromUtf8(plug.name));
            act->setData(QVariant(plug.pluginId));
        }
    }

    if (!hasInstruments) {
        QAction* warn = menu.addAction(tr("(No scanned instruments found)"));
        warn->setEnabled(false);
    }

    QAction* chosen = menu.exec(QCursor::pos());
    if (!chosen) return;

    uint32_t pluginId = chosen->data().toUInt();
    if (pluginId > 0) {
        QString trackName = chosen->text();
        TrackID id = m_ctrl.track->addInstrumentTrack(trackName.toUtf8().constData(), generateRandomTrackColor());
        if (id.isValid()) {
            m_ctrl.track->insertInstrument(id, pluginId);
            reloadTracks();

            uint8_t category = PluginCategory::INSTRUMENT;
            auto* dialog = new PluginEditorDialog(m_ctrl.track, id, 0, trackName, category, this, true);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        }
    }
}

void PlaylistWindow::onAddAudioTrackWithClipRequested(const QString& absolutePath, uint64_t dropFrame)
{
    onAddAudioTracksWithClipsRequested(QStringList{absolutePath}, dropFrame);
}

void PlaylistWindow::onAddAudioTracksWithClipsRequested(const QStringList& filePaths, uint64_t dropFrame)
{
    if (!m_ctrl.track || filePaths.isEmpty()) return;

    // Clear prior track and clip selections so only newly dropped items are selected
    m_ctrl.track->clearTrackSelection();
    if (m_canvas) {
        m_canvas->deselectAll();
    }

    std::vector<bridge::RegionID> newRegions;
    for (const QString& path : filePaths) {
        TrackID id = m_ctrl.track->addAudioTrack("Audio", 2, generateRandomTrackColor());
        if (id.isValid()) {
            m_ctrl.track->setTrackSelected(id, true);
            if (m_ctrl.arrangement) {
                bridge::RegionID regionId = m_ctrl.arrangement->importAudioClip(id, path.toUtf8().constData(), dropFrame);
                if (regionId.isValid()) {
                    newRegions.push_back(regionId);
                }
            }
        }
    }

    if (m_canvas && !newRegions.empty()) {
        m_canvas->selectRegions(newRegions);
    }

    reloadTracks();
}

void PlaylistWindow::onAddInstrumentTrackWithPluginRequested(uint32_t pluginId)
{
    if (!m_ctrl.track) return;
    m_ctrl.track->clearTrackSelection();
    TrackID id = m_ctrl.track->addInstrumentTrack("Instrument", generateRandomTrackColor());
    if (id.isValid()) {
        m_ctrl.track->setTrackSelected(id, true);
        m_ctrl.track->insertInstrument(id, pluginId);
    }
    reloadTracks();
}

void PlaylistWindow::onInsertInstrumentRequested(TrackID id, uint32_t pluginId)
{
    if (!m_ctrl.track) return;
    m_ctrl.track->insertInstrument(id, pluginId);
    reloadTracks();
}

void PlaylistWindow::onInsertPluginRequested(TrackID id, uint32_t pluginId)
{
    if (!m_ctrl.track) return;
    bridge::TrackUIState state = m_ctrl.track->getTrackState(id);
    
    // Find the first empty slot
    uint32_t slotIndex = bridge::MAX_PLUGIN_SLOTS;
    for (uint32_t i = 0; i < bridge::MAX_PLUGIN_SLOTS; ++i) {
        if (!state.plugins[i].pluginNodeId.isValid()) {
            slotIndex = i;
            break;
        }
    }

    // Fall back to replacing the last slot if all slots are full
    if (slotIndex >= bridge::MAX_PLUGIN_SLOTS) {
        slotIndex = bridge::MAX_PLUGIN_SLOTS - 1;
    }

    m_ctrl.track->insertPlugin(id, slotIndex, pluginId);
    reloadTracks();
}

void PlaylistWindow::onTrackMuteToggled(TrackID id, bool mute)
{
    if (m_ctrl.track) {
        m_ctrl.track->setMute(id, mute);
    }
}

void PlaylistWindow::onTrackSoloToggled(TrackID id, bool solo)
{
    if (m_ctrl.track) {
        m_ctrl.track->setSolo(id, solo);
    }
}

void PlaylistWindow::onTrackArmToggled(TrackID id, bool armed)
{
    if (m_ctrl.track) {
        m_ctrl.track->setRecordArmed(id, armed);
    }
}

void PlaylistWindow::onTrackInputMonitorToggled(TrackID id, bool enabled)
{
    if (m_ctrl.track) {
        m_ctrl.track->setInputMonitoring(id, enabled);
    }
}

void PlaylistWindow::onTrackRenameRequested(TrackID id, const QString& newName)
{
    if (m_ctrl.track) {
        m_ctrl.track->renameTrack(id, newName.toUtf8().constData());
    }
}

void PlaylistWindow::onTrackColorChangeRequested(TrackID id, uint32_t colorARGB)
{
    if (m_ctrl.track) {
        m_ctrl.track->setTrackColor(id, colorARGB);
    }
}

void PlaylistWindow::onTrackMoveRequested(TrackID id, uint32_t newIndex, TrackID newParentFolderId)
{
    if (m_ctrl.track) {
        m_ctrl.track->moveTrack(id, newIndex, newParentFolderId);
        reloadTracks();
    }
}

void PlaylistWindow::onTrackDeleteRequested(TrackID id)
{
    if (m_ctrl.track) {
        m_ctrl.track->removeTrack(id);
        reloadTracks();
    }
}

void PlaylistWindow::onConfigureAutomationRequested(TrackID id)
{
    auto* window = new ParameterWindow(id, m_ctrl.track, m_ctrl.automation, this);
    connect(window, &ParameterWindow::laneAdded, this, &PlaylistWindow::reloadTracks);
    window->show();
}

// ---------------------------------------------------------------------------
// Clip / Region Mutation Handlers
// ---------------------------------------------------------------------------

void PlaylistWindow::onRegionMoveRequested(bridge::RegionID id,
                                           TrackID destTrack,
                                           int64_t newStart,
                                           uint32_t destLayer)
{
    if (m_ctrl.arrangement) {
        m_ctrl.arrangement->moveRegion(id, destTrack, newStart, destLayer);
        reloadTracks();
    }
}

void PlaylistWindow::onRegionSplitRequested(bridge::RegionID id, uint64_t splitFrame)
{
    if (m_ctrl.arrangement) {
        m_ctrl.arrangement->splitRegion(id, splitFrame);
    }
}

void PlaylistWindow::onRegionDeleteRequested(bridge::RegionID id)
{
    if (m_ctrl.arrangement) {
        bridge::VisualRegion region{};
        if (m_ctrl.arrangement->getVisualRegion(id, region)) {
            if (m_canvas) {
                m_canvas->invalidateMedia(MediaID::fromRaw(region.mediaId));
            }
        }
        m_ctrl.arrangement->deleteRegion(id);
    }
}

void PlaylistWindow::onRegionGainChanged(bridge::RegionID id, float gainLinear)
{
    if (m_ctrl.arrangement) {
        m_ctrl.arrangement->setRegionGain(id, gainLinear);
    }
}

void PlaylistWindow::onRegionFadesChanged(bridge::RegionID id,
                                           uint32_t fadeIn,
                                           uint32_t fadeOut)
{
    if (m_ctrl.arrangement) {
        m_ctrl.arrangement->setRegionFades(id, fadeIn, fadeOut);
    }
}

// ---------------------------------------------------------------------------
// Viewport / Timeline Handlers
// ---------------------------------------------------------------------------

void PlaylistWindow::onSeekRequested(uint64_t frame)
{
    if (m_ctrl.timeline) m_ctrl.timeline->seekToFrame(frame);
}

void PlaylistWindow::onZoomScrollChanged(uint64_t newStart, double newZoom)
{
    m_viewport.startFrame = newStart;
    m_viewport.zoomFactor = newZoom;
    recalculateViewportEndFrame();
    applyViewport();
}

void PlaylistWindow::onViewportVerticalScrolled(int offsetPx)
{
    m_viewport.verticalOffsetPx = offsetPx;
    applyViewport();
}

void PlaylistWindow::onTempoCollapseToggled(bool collapsed)
{
    m_tempoCollapsed = collapsed;
    if (m_tempoHeader) {
        m_tempoHeader->setCollapsed(collapsed);
    }
    if (m_tempoRow) {
        m_tempoRow->setFixedHeight(collapsed ? 12 : m_expandedHeight);
    }
}

void PlaylistWindow::onTempoHeightResizeRequested(int newHeight)
{
    if (m_tempoCollapsed) return;
    m_expandedHeight = std::clamp(newHeight, 40, 300);
    if (m_tempoRow) {
        m_tempoRow->setFixedHeight(m_expandedHeight);
    }
}

void PlaylistWindow::recalculateViewportEndFrame()
{
    if (m_canvas && m_ctrl.timeline) {
        double canvasWidth = static_cast<double>(m_canvas->width());
        uint64_t framesVisible = static_cast<uint64_t>(
            m_ctrl.timeline->pixelsToFrames(static_cast<float>(canvasWidth),
                                            static_cast<float>(m_viewport.zoomFactor)));
        m_viewport.endFrame = m_viewport.startFrame + framesVisible;
    }
}

void PlaylistWindow::zoomHorizontal(double zoomScale)
{
    double oldZoom = m_viewport.zoomFactor;
    double newZoom = std::max(0.0001, std::min(1.0, oldZoom * zoomScale));
    if (newZoom == oldZoom) return;

    double centerX = 0.0;
    if (m_canvas) {
        centerX = static_cast<double>(m_canvas->width()) / 2.0;
    }

    double centerFrame = static_cast<double>(m_viewport.startFrame) + (centerX / oldZoom);
    double newStartFrameD = centerFrame - (centerX / newZoom);
    uint64_t clampedStart = (newStartFrameD >= 0.0) ? static_cast<uint64_t>(newStartFrameD) : 0;

    m_viewport.startFrame = clampedStart;
    m_viewport.zoomFactor = newZoom;
    recalculateViewportEndFrame();
    applyViewport();
}

void PlaylistWindow::zoomReset()
{
    m_viewport.zoomFactor = 0.001;
    recalculateViewportEndFrame();
    applyViewport();
}

void PlaylistWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    recalculateViewportEndFrame();
    applyViewport();
}

void PlaylistWindow::onAnalyzeLoudnessRequested()
{
    if (!m_ctrl.render || !m_ctrl.timeline) return;

    // Get current arrangement length for full song analysis
    uint64_t startFrame = 0;
    uint64_t endFrame = 0;
    if (m_ctrl.arrangement) {
        endFrame = m_ctrl.arrangement->getArrangementLength();
    }
    
    // If empty arrangement, default to 5 minutes fallback at timeline sample rate
    if (endFrame == 0) {
        double sr = m_ctrl.timeline->getSampleRate();
        endFrame = static_cast<uint64_t>(sr * 300); // 5 min
    }

    uint32_t sampleRate = static_cast<uint32_t>(m_ctrl.timeline->getSampleRate());

    // Start the silent mix analysis
    m_ctrl.render->startSilentMixAnalysis(startFrame, endFrame, sampleRate, 0);

    // Create a progress dialog
    QProgressDialog progressDialog(tr("Analyzing mix loudness..."), tr("Cancel"), 0, 100, this);
    progressDialog.setWindowTitle(tr("Mix Statistics & Diagnostics"));
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setMinimumDuration(0);
    progressDialog.setValue(0);

    // Style the progress dialog to match the dark theme
    progressDialog.setStyleSheet(
        "QProgressDialog { background-color: #1a1b24; color: #a0a5b5; }"
        "QLabel { color: #a0a5b5; font-family: monospace; }"
        "QProgressBar { border: 1px solid #2e3044; background: #0f1015; text-align: center; color: #00FFCC; }"
        "QProgressBar::chunk { background-color: #00FFCC; }"
        "QPushButton { background-color: #2e3044; color: #ffffff; border: 1px solid #3d4059; border-radius: 3px; padding: 4px 8px; }"
        "QPushButton:hover { background-color: #3d4059; }"
    );

    // Poll render progress until finished or cancelled
    QTimer* timer = new QTimer(&progressDialog);
    connect(timer, &QTimer::timeout, this, [this, &progressDialog, timer]() {
        if (progressDialog.wasCanceled()) {
            m_ctrl.render->cancelOfflineRender();
            timer->stop();
            progressDialog.close();
            return;
        }

        char errorMsg[256];
        if (m_ctrl.render->hasFailed(errorMsg, sizeof(errorMsg))) {
            timer->stop();
            progressDialog.close();
            QMessageBox::critical(this, tr("Analysis Failed"), QString::fromUtf8(errorMsg));
            return;
        }

        if (!m_ctrl.render->isRenderingActive()) {
            timer->stop();
            progressDialog.setValue(100);
            progressDialog.close();

            // Fetch results from the project lifecycle controller
            if (m_ctrl.lifecycle) {
                auto stats = m_ctrl.lifecycle->getMixStatisticsState();
                if (stats.isAnalyzed) {
                    QString infoText = QString(
                        "<h3>Mix Analysis Results</h3>"
                        "<p><b>Integrated Loudness:</b> %1 LUFS</p>"
                        "<p><b>True Peak Level:</b> %2 dBTP</p>"
                        "<p><b>Clipping Status:</b> %3</p>"
                    ).arg(QString::number(static_cast<double>(stats.integratedLoudnessLUFS), 'f', 1))
                     .arg(QString::number(static_cast<double>(stats.truePeakDBTP), 'f', 1))
                     .arg(stats.clippingDetected ? "<font color='#FF5555'><b>CLIPPING DETECTED</b></font>" : "<font color='#55FF55'>No Clipping Detected</font>");

                    QMessageBox msgBox(QMessageBox::Information, tr("Analysis Complete"), "", QMessageBox::Ok, this);
                    msgBox.setTextFormat(Qt::RichText);
                    msgBox.setInformativeText(infoText);
                    msgBox.setStyleSheet(
                        "QMessageBox { background-color: #1a1b24; color: #a0a5b5; }"
                        "QLabel { color: #a0a5b5; }"
                        "QPushButton { background-color: #2e3044; color: #ffffff; border: 1px solid #3d4059; border-radius: 3px; padding: 4px 8px; }"
                        "QPushButton:hover { background-color: #3d4059; }"
                    );
                    msgBox.exec();
                }
            }
            return;
        }

        float progress = m_ctrl.render->getRenderProgress();
        progressDialog.setValue(static_cast<int>(progress * 100.0f));
    });

    timer->start(50);
    progressDialog.exec();
}

void PlaylistWindow::onSaveProject()
{
    if (!m_ctrl.lifecycle || !m_ctrl.lifecycle->hasActiveProject()) {
        return;
    }
    std::string currentPath = m_ctrl.lifecycle->getCurrentProjectPath();
    if (currentPath.empty()) {
        onSaveProjectAs();
    } else {
        bool success = m_ctrl.lifecycle->saveProject(currentPath.c_str());
        if (!success) {
            QMessageBox::critical(this, tr("Save Project Error"),
                                  tr("Failed to save the project to: %1")
                                  .arg(QString::fromStdString(currentPath)));
        } else {
            qDebug() << "Project saved successfully to:" << QString::fromStdString(currentPath);
        }
    }
}

void PlaylistWindow::onSaveProjectAs()
{
    if (!m_ctrl.lifecycle || !m_ctrl.lifecycle->hasActiveProject()) {
        return;
    }
    
    QString defaultDir = QStringLiteral("./projects");
    QDir().mkpath(defaultDir);

    QString fileFilter = tr("%1 Project Files (*.ssproj *.strata *.agdaw *.adaw)").arg(QString::fromUtf8(config::PROJECT_DISPLAY_NAME.data(), static_cast<qsizetype>(config::PROJECT_DISPLAY_NAME.size())));
    QString selectedPath = QFileDialog::getSaveFileName(
        this,
        tr("Save Project As"),
        defaultDir,
        fileFilter
    );

    if (!selectedPath.isEmpty()) {
        if (!selectedPath.endsWith(".agdaw") && !selectedPath.endsWith(".adaw")) {
            selectedPath += ".agdaw";
        }

        bool success = m_ctrl.lifecycle->saveProject(selectedPath.toUtf8().constData());
        if (!success) {
            QMessageBox::critical(this, tr("Save Project Error"),
                                  tr("Failed to save the project to: %1")
                                  .arg(selectedPath));
        } else {
            qDebug() << "Project saved successfully to:" << selectedPath;
        }
    }
}

void PlaylistWindow::onOpenProject()
{
    if (!m_ctrl.lifecycle) return;

    QString defaultDir = QStringLiteral("./projects");
    QDir().mkpath(defaultDir);

    QString fileFilter = tr("%1 Project Files (*.ssproj *.strata *.agdaw *.adaw)").arg(QString::fromUtf8(config::PROJECT_DISPLAY_NAME.data(), static_cast<qsizetype>(config::PROJECT_DISPLAY_NAME.size())));
    QString selectedPath = QFileDialog::getOpenFileName(
        this,
        tr("Open Project"),
        defaultDir,
        fileFilter
    );

    if (selectedPath.isEmpty()) {
        return;
    }

    bool success = m_ctrl.lifecycle->loadProject(selectedPath.toUtf8().constData());
    if (!success) {
        QMessageBox::critical(this, tr("Open Project Error"),
                              tr("Failed to load the project from: %1")
                              .arg(selectedPath));
        return;
    }

    qDebug() << "Project loaded successfully from:" << selectedPath;

    // Check for missing plugins
    auto missing = m_ctrl.lifecycle->getMissingPluginsFromLastLoad();
    if (!missing.empty()) {
        QString text = "<h3>Missing Plugins Detected</h3>"
                       "<p>The project loaded successfully, but some plugins referenced in the project are not available on this machine:</p>"
                       "<ul>";
        for (const auto& item : missing) {
            QString slotName = item.slotIndex == -1 ? "Instrument" : QString("Slot %1").arg(item.slotIndex);
            text += QString("<li><b>%1</b> (Track ID: %2, %3, ID: %4)</li>")
                    .arg(QString::fromStdString(item.originalName))
                    .arg(item.trackId.toRaw())
                    .arg(slotName)
                    .arg(item.requestedPluginId);
        }
        text += "</ul>";

        QMessageBox msgBox(QMessageBox::Warning, tr("Missing Plugins"), "", QMessageBox::Ok, this);
        msgBox.setTextFormat(Qt::RichText);
        msgBox.setInformativeText(text);
        msgBox.setStyleSheet(
            "QMessageBox { background-color: #1a1b24; color: #a0a5b5; }"
            "QLabel { color: #a0a5b5; }"
            "QPushButton { background-color: #2e3044; color: #ffffff; border: 1px solid #3d4059; border-radius: 3px; padding: 4px 8px; }"
            "QPushButton:hover { background-color: #3d4059; }"
        );
        msgBox.exec();
    }
}

void PlaylistWindow::onExportProjectJson()
{
    if (!m_ctrl.lifecycle || !m_ctrl.lifecycle->hasActiveProject()) {
        return;
    }
    
    QString defaultDir = QStringLiteral("./projects");
    QDir().mkpath(defaultDir);

    QString fileFilter = tr("JSON Files (*.json)");
    QString selectedPath = QFileDialog::getSaveFileName(
        this,
        tr("Export Session to JSON"),
        defaultDir,
        fileFilter
    );

    if (!selectedPath.isEmpty()) {
        if (!selectedPath.endsWith(".json")) {
            selectedPath += ".json";
        }

        bool success = m_ctrl.lifecycle->exportProjectToJson(selectedPath.toUtf8().constData());
        if (!success) {
            QMessageBox::critical(this, tr("Export JSON Error"),
                                  tr("Failed to export the project to JSON: %1")
                                  .arg(selectedPath));
        } else {
            qDebug() << "Project exported to JSON successfully to:" << selectedPath;
        }
    }
}

void PlaylistWindow::onImportProjectJson()
{
    if (!m_ctrl.lifecycle) return;

    QString defaultDir = QStringLiteral("./projects");
    QDir().mkpath(defaultDir);

    QString fileFilter = tr("JSON Files (*.json)");
    QString selectedPath = QFileDialog::getOpenFileName(
        this,
        tr("Import Session from JSON"),
        defaultDir,
        fileFilter
    );

    if (selectedPath.isEmpty()) {
        return;
    }

    bool success = m_ctrl.lifecycle->importProjectFromJson(selectedPath.toUtf8().constData());
    if (!success) {
        QMessageBox::critical(this, tr("Import JSON Error"),
                              tr("Failed to import the project from JSON: %1")
                              .arg(selectedPath));
        return;
    }

    qDebug() << "Project imported from JSON successfully from:" << selectedPath;

    // Check for missing plugins
    auto missing = m_ctrl.lifecycle->getMissingPluginsFromLastLoad();
    if (!missing.empty()) {
        QString text = "<h3>Missing Plugins Detected</h3>"
                       "<p>The project imported successfully, but some plugins referenced in the project are not available on this machine:</p>"
                       "<ul>";
        for (const auto& item : missing) {
            QString slotName = item.slotIndex == -1 ? "Instrument" : QString("Slot %1").arg(item.slotIndex);
            text += QString("<li><b>%1</b> (Track ID: %2, %3, ID: %4)</li>")
                    .arg(QString::fromStdString(item.originalName))
                    .arg(item.trackId.toRaw())
                    .arg(slotName)
                    .arg(item.requestedPluginId);
        }
        text += "</ul>";

        QMessageBox msgBox(QMessageBox::Warning, tr("Missing Plugins"), "", QMessageBox::Ok, this);
        msgBox.setTextFormat(Qt::RichText);
        msgBox.setInformativeText(text);
        msgBox.setStyleSheet(
            "QMessageBox { background-color: #1a1b24; color: #a0a5b5; }"
            "QLabel { color: #a0a5b5; }"
            "QPushButton { background-color: #2e3044; color: #ffffff; border: 1px solid #3d4059; border-radius: 3px; padding: 4px 8px; }"
            "QPushButton:hover { background-color: #3d4059; }"
        );
        msgBox.exec();
    }
}

void PlaylistWindow::buildArrangementsMenu() {
    m_arrangementsMenu->clear();

    if (m_ctrl.arrangementManager) {
        auto arrangements = m_ctrl.arrangementManager->getArrangements();
        ArrangementID activeId = m_ctrl.arrangementManager->getActiveArrangement();

        for (const auto& arr : arrangements) {
            QString nameStr = QString::fromUtf8(arr.name);
            QAction* act = m_arrangementsMenu->addAction(nameStr);
            act->setCheckable(true);
            act->setChecked(arr.id == activeId);

            if (arr.id == activeId) {
                QFontMetrics fm(m_arrangementsBtn->font());
                QString elidedName = fm.elidedText(nameStr.toUpper(), Qt::ElideRight, 180);
                m_arrangementsBtn->setText(elidedName);
                m_arrangementsBtn->setToolTip(nameStr);
            }

            connect(act, &QAction::triggered, this, [this, id = arr.id]() {
                if (m_ctrl.arrangementManager) {
                    m_ctrl.arrangementManager->setActiveArrangement(id);
                }
            });
        }

        if (!arrangements.empty()) {
            m_arrangementsMenu->addSeparator();
        }
    }

    QAction* addAction = m_arrangementsMenu->addAction("Add Arrangement");
    addAction->setData(QStringLiteral("add"));

    m_arrangementsMenu->addSeparator();

    QAction* renameAction = m_arrangementsMenu->addAction("Rename…");
    renameAction->setData(QStringLiteral("rename"));

    QAction* cloneAction = m_arrangementsMenu->addAction("Clone");
    cloneAction->setData(QStringLiteral("clone"));

    m_arrangementsMenu->addSeparator();

    QAction* mergeAction = m_arrangementsMenu->addAction("Merge Arrangements…");
    mergeAction->setData(QStringLiteral("merge"));

    m_arrangementsMenu->addSeparator();

    QAction* deleteAction = m_arrangementsMenu->addAction("Delete");
    deleteAction->setData(QStringLiteral("delete"));
}

void PlaylistWindow::onArrangementsMenuTriggered(QAction* action) {
    if (!action) return;

    const QString id = action->data().toString();

    if (id == QStringLiteral("add")) {
        bool ok = false;
        QString name = DAWInputDialog::getText(this, tr("Add Arrangement"),
                                             tr("Arrangement Name:"),
                                             "", &ok);
        if (ok && !name.trimmed().isEmpty()) {
            if (m_ctrl.arrangementManager) {
                m_ctrl.arrangementManager->createArrangement(name.trimmed().toUtf8().constData());
            }
        }
    } else if (id == QStringLiteral("rename")) {
        bool ok = false;
        QString name = DAWInputDialog::getText(this, tr("Rename Arrangement"),
                                             tr("New Arrangement Name:"),
                                             "", &ok);
        if (ok && !name.trimmed().isEmpty()) {
            if (m_ctrl.arrangementManager) {
                ArrangementID activeId = m_ctrl.arrangementManager->getActiveArrangement();
                if (activeId.isValid()) {
                    m_ctrl.arrangementManager->renameArrangement(activeId, name.trimmed().toUtf8().constData());
                }
            }
        }
    } else if (id == QStringLiteral("clone")) {
        bool ok = false;
        QString name = DAWInputDialog::getText(this, tr("Clone Arrangement"),
                                             tr("Name for Clone:"),
                                             "", &ok);
        if (ok && !name.trimmed().isEmpty()) {
            if (m_ctrl.arrangementManager) {
                ArrangementID activeId = m_ctrl.arrangementManager->getActiveArrangement();
                if (activeId.isValid()) {
                    m_ctrl.arrangementManager->cloneArrangement(activeId, name.trimmed().toUtf8().constData());
                }
            }
        }
    } else if (id == QStringLiteral("merge")) {
        MergeArrangementDialog dialog(m_ctrl.arrangementManager, this);
        if (dialog.exec() == QDialog::Accepted) {
            MergeFilterOptions filterOptions{};
            filterOptions.importAudio = dialog.getImportAudio();
            filterOptions.importMIDI = dialog.getImportMIDI();
            filterOptions.importAutomation = dialog.getImportAutomation();
            filterOptions.importMixerSettings = dialog.getImportMixerSettings();
            filterOptions.limitToLoopRange = dialog.getLimitToLoop();
            if (m_ctrl.timeline) {
                filterOptions.loopStartFrame = m_ctrl.timeline->getLoopStart();
                filterOptions.loopEndFrame = m_ctrl.timeline->getLoopEnd();
            } else {
                filterOptions.loopStartFrame = 0;
                filterOptions.loopEndFrame = 0;
            }

            if (m_ctrl.arrangementManager) {
                m_ctrl.arrangementManager->mergeArrangements(
                    dialog.getSourceArrangementId(),
                    dialog.getDestinationArrangementId(),
                    dialog.getMergeMode(),
                    filterOptions
                );
            }
            reloadTracks();
        }
    } else if (id == QStringLiteral("delete")) {
        auto button = QMessageBox::question(this, tr("Delete Arrangement"),
                                            tr("Are you sure you want to delete the active arrangement? This action cannot be undone."),
                                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (button == QMessageBox::Yes) {
            if (m_ctrl.arrangementManager) {
                ArrangementID activeId = m_ctrl.arrangementManager->getActiveArrangement();
                if (activeId.isValid()) {
                    m_ctrl.arrangementManager->deleteArrangement(activeId);
                }
            }
        }
    }
}

void PlaylistWindow::wheelEvent(QWheelEvent* event)
{
    const QPointF angleDelta = event->angleDelta();
    if (angleDelta.isNull()) {
        event->accept();
        return;
    }

    const bool isZoom = event->modifiers() & Qt::ControlModifier;
    const QPoint numPixels = event->pixelDelta();
    const QPoint numDegrees = event->angleDelta() / 8;

    double deltaX = 0.0;
    double deltaY = 0.0;

    if (!numPixels.isNull()) {
        deltaX = static_cast<double>(numPixels.x());
        deltaY = static_cast<double>(numPixels.y());
    } else if (!numDegrees.isNull()) {
        deltaX = static_cast<double>(numDegrees.x()) * 1.5;
        deltaY = static_cast<double>(numDegrees.y()) * 1.5;
    }

    if (isZoom) {
        // Zoom centered around current mouse position relative to canvas
        double mouseX = 0.0;
        if (m_canvas) {
            // Map global mouse position to canvas coordinate space
            QPointF canvasLocalPos = m_canvas->mapFromGlobal(event->globalPosition());
            mouseX = std::max(0.0, canvasLocalPos.x());
        }

        // Convert mouseX to timeline frame
        uint64_t cursorFrame = m_viewport.startFrame;
        if (m_viewport.zoomFactor > 0.0) {
            double frameD = m_viewport.startFrame + mouseX / m_viewport.zoomFactor;
            cursorFrame = (frameD >= 0.0) ? static_cast<uint64_t>(frameD) : 0;
        }

        const double zoomScale = 1.0 + (deltaY * 0.002);
        const double newZoom = std::max(0.0001, std::min(1.0, m_viewport.zoomFactor * zoomScale));

        // Re-anchor start frame so mouse remains over the exact same musical position
        const double newStartFrameD = static_cast<double>(cursorFrame) - (mouseX / newZoom);
        const uint64_t clampedStart = static_cast<uint64_t>(std::max(0.0, newStartFrameD));

        onZoomScrollChanged(clampedStart, newZoom);
    } else {
        // Scroll horizontally (using Shift modifier or trackpad horizontal axis)
        const bool isHorizontalScroll = (event->modifiers() & Qt::ShiftModifier) || (std::abs(deltaX) > std::abs(deltaY));
        if (isHorizontalScroll) {
            const double scrollFactor = (event->modifiers() & Qt::ShiftModifier) ? 5.0 : 1.0;
            const int64_t frameDelta  = static_cast<int64_t>((deltaX - deltaY) * 2.0 * scrollFactor / m_viewport.zoomFactor);
            const int64_t newStart    = static_cast<int64_t>(m_viewport.startFrame) + frameDelta;
            const uint64_t clampedStart = static_cast<uint64_t>(std::max(int64_t{0}, newStart));

            onZoomScrollChanged(clampedStart, m_viewport.zoomFactor);
        } else {
            // Vertical scroll (scroll track lanes)
            const int scrollDeltaY = static_cast<int>(-deltaY);
            int newOffset = std::max(0, m_viewport.verticalOffsetPx + scrollDeltaY);
            onViewportVerticalScrolled(newOffset);
        }
    }
    event->accept();
}

bool PlaylistWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Paint) {
        if (watched == this->window()) {
            m_frameCount++;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlaylistWindow::onImportAudioFile() {
    if (!m_ctrl.arrangement || !m_ctrl.timeline) return;
    QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Import Audio File"), QString(), QStringLiteral("Audio Files (*.wav *.mp3 *.flac *.ogg *.aiff)"));
    if (!file.isEmpty()) {
        TrackID targetTrack = TrackID{1, 0};
        uint64_t frame = m_ctrl.timeline->getCurrentFrame();
        m_ctrl.arrangement->importAudioClip(targetTrack, file.toUtf8().constData(), frame);
        if (m_canvas) m_canvas->update();
    }
}

void PlaylistWindow::onInsertBlankMidiClip() {
    if (!m_ctrl.arrangement || !m_ctrl.timeline) return;
    TrackID targetTrack = TrackID{1, 0};
    uint64_t frame = m_ctrl.timeline->getCurrentFrame();
    uint64_t duration = m_ctrl.timeline->ticksToSamples(1920); // 4 bars default
    if (duration == 0) duration = 44100 * 4;
    m_ctrl.arrangement->insertMidiClip(targetTrack, frame, duration);
    if (m_canvas) m_canvas->update();
}

} // namespace presentation::views
