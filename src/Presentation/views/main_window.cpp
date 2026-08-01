#include "main_window.h"
#include <project_config.h>
#include "theme.h"
#include "browser_window/BrowserWidget.h"
#include "playlist/PlaylistWindow.h"
#include "pianoroll_window/PianoRollWindow.h"
#include "top_control_panel/top_control_panel.h"
#include "mixer/mixer_window.h"
#include "settings/settings_dialog.h"
#include "../shortcuts/ShortcutManager.h"
#include <QShortcut>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QPainter>
#include <QPen>
#include <QLabel>
#include <QScreen>
#include <QGuiApplication>
#include <cstdlib>
#include <cmath>

namespace presentation::views {

// --- MainWindow Implementation ---

MainWindow::~MainWindow() = default;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    // 1. Sleek geometric default window sizing - dynamically use max screen resolution
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        resize(screen->availableGeometry().size());
        setWindowState(Qt::WindowMaximized);
    } else {
        resize(1280, 768);
    }
    setWindowTitle(QString::fromUtf8(config::PROJECT_DISPLAY_NAME.data(), static_cast<qsizetype>(config::PROJECT_DISPLAY_NAME.size())));
    
    // 2. Load and apply unified Industrial QSS rules
    setStyleSheet(theme::Style::getGlobalStyleSheet());

    // 3. Set central container widget with vertical layout (toolbar + content)
    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setSpacing(0);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    // 3a. Top Control Panel (toolbar)
    m_topControlPanel = new TopControlPanel(this);
    m_topControlPanel->setFixedHeight(48);
    rootLayout->addWidget(m_topControlPanel);

    // 3b. Main content area containing a horizontal layout
    QWidget* contentWidget = new QWidget(centralWidget);
    QHBoxLayout* mainLayout = new QHBoxLayout(contentWidget);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(contentWidget, 1); // stretch factor 1

    // 3c. Splitter splitting vertically (Playlist on top, Mixer at bottom)
    m_mainSplitter = new QSplitter(Qt::Vertical, contentWidget);
    m_mainSplitter->setHandleWidth(2);
    m_mainSplitter->setStyleSheet("QSplitter::handle { background-color: #526D82; }");
    mainLayout->addWidget(m_mainSplitter, 1);

    // 3d. Professional Mixer Window (independent floating window)
    m_mixerWindow = new MixerWindow(this);
    m_mixerWindow->setWindowFlags(Qt::Window);
    connect(m_mixerWindow, &MixerWindow::closedByUser, this, [this]() {
        if (m_workspaceController) {
            m_workspaceController->setWindowVisible(bridge::WorkspaceWindow::Mixer, false);
        }
    });

    setCentralWidget(centralWidget);

    // Global Navigation & View Shortcuts via ShortcutManager
    auto& sm = presentation::shortcuts::ShortcutManager::instance();
    using presentation::shortcuts::ShortcutAction;

    sm.bind(this, ShortcutAction::Global_ToggleArrangement, [this]() {
        toggleWindowVisibility(bridge::WorkspaceWindow::Arrangement);
    }, Qt::ApplicationShortcut);
    sm.bindSequence(this, QKeySequence("Alt+1"), [this]() {
        toggleWindowVisibility(bridge::WorkspaceWindow::Arrangement);
    }, Qt::ApplicationShortcut);

    sm.bind(this, ShortcutAction::Global_TogglePianoRoll, [this]() {
        toggleWindowVisibility(bridge::WorkspaceWindow::PianoRoll);
    }, Qt::ApplicationShortcut);
    sm.bindSequence(this, QKeySequence("Alt+2"), [this]() {
        toggleWindowVisibility(bridge::WorkspaceWindow::PianoRoll);
    }, Qt::ApplicationShortcut);

    sm.bind(this, ShortcutAction::Global_ToggleMixer, [this]() {
        toggleWindowVisibility(bridge::WorkspaceWindow::Mixer);
    }, Qt::ApplicationShortcut);
    sm.bindSequence(this, QKeySequence("Alt+3"), [this]() {
        toggleWindowVisibility(bridge::WorkspaceWindow::Mixer);
    }, Qt::ApplicationShortcut);

    sm.bind(this, ShortcutAction::Global_ToggleMetronome, [this]() {
        if (m_timelineController) {
            m_timelineController->setMetronomeEnabled(!m_timelineController->isMetronomeEnabled());
        }
    }, Qt::ApplicationShortcut);

    // Global Transport Play/Pause Shortcut (Space)
    sm.bindSequence(this, QKeySequence("Space"), [this]() {
        if (m_timelineController) {
            m_timelineController->togglePlay();
        }
    }, Qt::ApplicationShortcut);

    // Global Undo / Redo Shortcuts
    sm.bind(this, ShortcutAction::Global_Undo, [this]() {
        if (m_arrangementController) {
            if (m_arrangementController->undo()) {
                if (m_playlistWindow) m_playlistWindow->reloadTracks();
                if (m_pianoRollWindow) m_pianoRollWindow->refreshView();
                syncMixerStrips();
            }
        }
    }, Qt::ApplicationShortcut);

    sm.bind(this, ShortcutAction::Global_Redo, [this]() {
        if (m_arrangementController) {
            if (m_arrangementController->redo()) {
                if (m_playlistWindow) m_playlistWindow->reloadTracks();
                if (m_pianoRollWindow) m_pianoRollWindow->refreshView();
                syncMixerStrips();
            }
        }
    }, Qt::ApplicationShortcut);

    // 4. Register MixerWindow for 60Hz updates in director
    m_director.setMixerWindow(m_mixerWindow);

    // 5. Spin up 60Hz centralized GUI telemetry loop
    m_director.start();
}

void MainWindow::setMeteringProvider(bridge::IMeteringProvider* provider) {
    // Allows switching to live Audio Engine telemetry at runtime
    if (provider) {
        m_director.setMeteringProvider(provider);
    }
}

void MainWindow::setBrowserController(bridge::IBrowserController* controller) {
    if (!controller) return;

    if (!m_browserWidget) {
        m_browserWidget = new BrowserWidget(controller, this);

        // Find the content widget's horizontal layout (second child of root layout)
        auto* rootLayout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
        if (rootLayout && rootLayout->count() > 1) {
            QWidget* contentWidget = rootLayout->itemAt(1)->widget();
            if (contentWidget) {
                QHBoxLayout* mainLayout = qobject_cast<QHBoxLayout*>(contentWidget->layout());
                if (mainLayout) {
                    // Add vertical line divider
                    m_browserDivider = new QFrame(this);
                    m_browserDivider->setFrameShape(QFrame::VLine);
                    m_browserDivider->setStyleSheet("background-color: #526D82; min-width: 1px; max-width: 1px; border: none;");

                    // Insert at index 0 and 1
                    mainLayout->insertWidget(0, m_browserWidget);
                    mainLayout->insertWidget(1, m_browserDivider);

                    // No longer shrinking the main window here
                }
            }
        }
    }
}

void MainWindow::setHardwareSettingsFacade(bridge::IHardwareSettingsFacade* facade) {
    m_hardwareSettingsFacade = facade;
}

void MainWindow::setTopPanelControllers(bridge::ITimelineController* timelineCtrl,
                                         bridge::IInputModeController* inputCtrl,
                                         bridge::IWorkspaceController* workspaceCtrl) {
    m_timelineController = timelineCtrl;
    m_workspaceController = workspaceCtrl;
    if (m_topControlPanel) {
        m_topControlPanel->bind(timelineCtrl, inputCtrl, workspaceCtrl);
        m_director.setTopControlPanel(m_topControlPanel);
        
        // Connect the Settings button trigger
        connect(m_topControlPanel, &TopControlPanel::settingsClicked, this, &MainWindow::showSettingsDialog);
        connect(m_topControlPanel, &TopControlPanel::windowToggleClicked, this, &MainWindow::onWindowToggleClicked);
        connect(m_topControlPanel, &TopControlPanel::analyzeLoudnessClicked, this, [this]() {
            if (m_playlistWindow) {
                m_playlistWindow->onAnalyzeLoudnessRequested();
            }
        });
    }
    if (m_pianoRollWindow) {
        m_pianoRollWindow->bindTimeline(timelineCtrl);
    }

    // Sync initial visibility states from workspace controller
    if (m_workspaceController) {
        if (m_pianoRollWindow) {
            m_pianoRollWindow->setVisible(m_workspaceController->isWindowVisible(bridge::WorkspaceWindow::PianoRoll));
        }
        if (m_mixerWindow) {
            m_mixerWindow->setVisible(m_workspaceController->isWindowVisible(bridge::WorkspaceWindow::Mixer));
        }
        if (m_browserWidget) {
            bool browserVisible = m_workspaceController->isWindowVisible(bridge::WorkspaceWindow::Browser);
            m_browserWidget->setVisible(browserVisible);
            if (m_browserDivider) {
                m_browserDivider->setVisible(browserVisible);
            }
        }
    }
}

void MainWindow::setPlaylistControllers(const PlaylistWindow::Controllers& playlistCtrls) {
    if (m_playlistWindow) {
        delete m_playlistWindow;
    }

    m_arrangementController = playlistCtrls.arrangement;

    m_playlistWindow = new PlaylistWindow(playlistCtrls, this);
    m_mainSplitter->insertWidget(0, m_playlistWindow);

    // Bind MixerWindow with controllers and metering
    if (m_mixerWindow) {
        m_mixerWindow->bind(playlistCtrls.track, playlistCtrls.metering, playlistCtrls.automation);
        connect(m_mixerWindow, &MixerWindow::tracksChangedRequested, this, &MainWindow::syncMixerStrips);
    }

    // Connect tracksChanged signal to rebuild strips and sync meters
    connect(m_playlistWindow, &PlaylistWindow::tracksChanged, this, &MainWindow::syncMixerStrips);
    connect(m_playlistWindow, &PlaylistWindow::trackStatesUpdated, m_mixerWindow, &MixerWindow::updateTrackStates);
    connect(m_playlistWindow, &PlaylistWindow::midiClipDoubleClicked, this, &MainWindow::onMidiClipDoubleClicked);
    
    // Wire up mixer drag-and-drop moves to the same pipeline as playlist
    connect(m_mixerWindow, &MixerWindow::trackMoveRequested, this, [this, playlistCtrls](TrackID id, uint32_t newIndex, TrackID newParentFolderId) {
        if (playlistCtrls.track) {
            playlistCtrls.track->moveTrack(id, newIndex, newParentFolderId);
        }
        if (m_playlistWindow) {
            m_playlistWindow->reloadTracks();
        }
    });

    // Set metering provider in director
    m_director.setMeteringProvider(playlistCtrls.metering);

    // Initial Mixer synchronization
    syncMixerStrips();

    // Give Playlist all space initially
    QList<int> sizes;
    sizes << 740;
    m_mainSplitter->setSizes(sizes);
}

void MainWindow::syncMixerStrips() {
    if (!m_mixerWindow) return;

    // 1. Tell MixerWindow to rebuild its channel strips
    m_mixerWindow->rebuildStrips();

    // 2. Clear old registrations in the presentation director
    m_director.clearTrackMeters();

    // 3. Register the new TelemetryMeter widgets
    for (auto* strip : m_mixerWindow->trackStrips()) {
        m_director.registerTrackMeter(strip->trackId(), strip->meter());
    }

    // 4. Register Master meter
    if (m_mixerWindow->masterStrip() && m_mixerWindow->masterStrip()->meter()) {
        m_director.registerMasterMeter(m_mixerWindow->masterStrip()->meter());
    }
}

void MainWindow::paintEvent(QPaintEvent* event) {
    QMainWindow::paintEvent(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw an ultra-faint high-tech carbon cross-hatching or diagonal vector grids in background
    QColor gridColor = theme::Color::BgControl;
    gridColor.setAlphaF(static_cast<float>(gridColor.alphaF() * 0.7f));
    QPen gridPen(gridColor, 1.0);
    painter.setPen(gridPen);

    // Draw simple subtle horizontal faceplate vectors at the margins
    painter.drawLine(0, 1, width(), 1);
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void MainWindow::showSettingsDialog() {
    if (!m_hardwareSettingsFacade) return;
    SettingsDialog dialog(m_hardwareSettingsFacade, this);
    dialog.exec();
}

void MainWindow::setMidiEditorController(bridge::IMidiEditorController* controller) {
    if (!controller) return;

    if (m_pianoRollWindow) {
        delete m_pianoRollWindow;
    }

    m_pianoRollWindow = new PianoRollWindow(controller, m_mainSplitter);
    if (m_timelineController) {
        m_pianoRollWindow->bindTimeline(m_timelineController);
    }
    m_mainSplitter->insertWidget(1, m_pianoRollWindow);
    m_pianoRollWindow->hide();
}

void MainWindow::onMidiClipDoubleClicked(TrackID trackId, bridge::RegionID regionId) {
    if (m_pianoRollWindow) {
        m_pianoRollWindow->openMidiClip(trackId, regionId);
        m_pianoRollWindow->show();
        if (m_workspaceController) {
            m_workspaceController->setWindowVisible(bridge::WorkspaceWindow::PianoRoll, true);
        }

        QList<int> sizes = m_mainSplitter->sizes();
        if (sizes.size() >= 2) {
            sizes[0] = 480;
            sizes[1] = 260;
            m_mainSplitter->setSizes(sizes);
        }
    }
}

void MainWindow::onWindowToggleClicked(bridge::WorkspaceWindow window) {
    if (!m_workspaceController) return;

    bool visible = m_workspaceController->isWindowVisible(window);

    switch (window) {
        case bridge::WorkspaceWindow::PianoRoll:
            if (m_pianoRollWindow) {
                m_pianoRollWindow->setVisible(visible);
            }
            break;
        case bridge::WorkspaceWindow::Mixer:
            if (m_mixerWindow) {
                m_mixerWindow->setVisible(visible);
            }
            break;
        case bridge::WorkspaceWindow::Browser:
            if (m_browserWidget) {
                m_browserWidget->setVisible(visible);
                if (m_browserDivider) {
                    m_browserDivider->setVisible(visible);
                }
            }
            break;
        default:
            break;
    }
}

void MainWindow::toggleWindowVisibility(bridge::WorkspaceWindow window) {
    if (!m_workspaceController) {
        if (window == bridge::WorkspaceWindow::Arrangement && m_playlistWindow) {
            bool vis = m_playlistWindow->isVisible() && m_playlistWindow->isActiveWindow();
            m_playlistWindow->setVisible(!vis);
            if (!vis) { m_playlistWindow->setFocus(); m_playlistWindow->raise(); }
        } else if (window == bridge::WorkspaceWindow::PianoRoll && m_pianoRollWindow) {
            bool vis = m_pianoRollWindow->isVisible() && m_pianoRollWindow->isActiveWindow();
            m_pianoRollWindow->setVisible(!vis);
            if (!vis) { m_pianoRollWindow->setFocus(); m_pianoRollWindow->raise(); }
        } else if (window == bridge::WorkspaceWindow::Mixer && m_mixerWindow) {
            bool vis = m_mixerWindow->isVisible() && m_mixerWindow->isActiveWindow();
            m_mixerWindow->setVisible(!vis);
            if (!vis) { m_mixerWindow->setFocus(); m_mixerWindow->raise(); }
        }
        return;
    }

    bool currentVis = m_workspaceController->isWindowVisible(window);
    bool isFocused = false;
    if (window == bridge::WorkspaceWindow::Arrangement && m_playlistWindow) isFocused = m_playlistWindow->hasFocus();
    if (window == bridge::WorkspaceWindow::PianoRoll && m_pianoRollWindow) isFocused = m_pianoRollWindow->hasFocus();
    if (window == bridge::WorkspaceWindow::Mixer && m_mixerWindow) isFocused = m_mixerWindow->hasFocus();

    bool newVis = !(currentVis && isFocused);
    m_workspaceController->setWindowVisible(window, newVis);
    onWindowToggleClicked(window);

    if (newVis) {
        if (window == bridge::WorkspaceWindow::Arrangement && m_playlistWindow) {
            m_playlistWindow->show();
            m_playlistWindow->setFocus();
            m_playlistWindow->raise();
        } else if (window == bridge::WorkspaceWindow::PianoRoll && m_pianoRollWindow) {
            m_pianoRollWindow->show();
            m_pianoRollWindow->setFocus();
            m_pianoRollWindow->raise();
        } else if (window == bridge::WorkspaceWindow::Mixer && m_mixerWindow) {
            m_mixerWindow->show();
            m_mixerWindow->setFocus();
            m_mixerWindow->raise();
        }
    }
}

} // namespace presentation::views
