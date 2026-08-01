// src/Presentation/views/playlist/PlaylistWindow.h
#pragma once

#include <QWidget>
#include <QTimer>
#include <QShortcut>
#include <QElapsedTimer>
#include <unordered_map>
#include "common/system_primitives.h"
#include "PlaylistEditTool.h"

class QPushButton;
class QMenu;
class QLabel;
class QAction;

// Bridge interfaces (Layer 7 may only import from Middle Bridge)
#include "timeline/iarrangement_controller.h"
#include "Middle Bridge/automation/automation_helpers.h"
#include "timeline/itimeline_controller.h"
#include "automation/iautomation_controller.h"
#include "engine/iinput_mode_controller.h"
#include "telemetry/imetering_provider.h"
#include "telemetry/iwaveform_cache_provider.h"
#include "telemetry/ipattern_data_provider.h"
#include "browser/ibrowser_controller.h"
#include "project/isession_manager.h"
#include "project/iproject_lifecycle_controller.h"
#include "timeline/iarrangement_manager_controller.h"
#include "engine/irender_controller.h"
#include "project/iworkspace_controller.h"

// Playlist sub-widget forward declarations (defined in later phases)
#include "PlaylistViewport.h"
#include "TrackLayout.h"

// Forward-declare sub-widgets to keep the header lean
namespace presentation::views {
    class PlaylistTimelineRuler;
    class TrackHeaderView;
    class PlaylistClipCanvas;
    class PickerPanel;
    class MiniPlaylistPreview;
    class ProjectPickerScreen;
    class TempoTrackHeader;
    class TempoTrackCanvas;
    class PlaylistMenuButton;
}

namespace presentation::views {

/**
 * @brief Root DI window for the Arrangement / Playlist view.
 *
 * PlaylistWindow is the composition root for all playlist sub-widgets.
 * It:
 *  - Receives all required bridge interfaces via the Controllers aggregate.
 *  - Owns the single 60 Hz QTimer that is the ONLY source of bridge polling.
 *  - Assembles the full layout topology described in Phase 1-B.
 *  - Propagates the shared PlaylistViewport to ruler and canvas.
 *  - Registers/unregisters itself as an ISessionChangeListener.
 *
 * Threading discipline:
 *  - All bridge calls happen on the GUI thread inside onRenderTick() or
 *    mouse/key event handlers. paintEvent() reads only cached member vars.
 */
class PlaylistWindow : public QWidget,
                       public bridge::ISessionChangeListener {
    Q_OBJECT

public:
    // ---------------------------------------------------------------------------
    // Dependency Injection Aggregate
    // ---------------------------------------------------------------------------
    struct Controllers {
        bridge::IArrangementController* arrangement{nullptr};
        bridge::ITrackController*       track{nullptr};
        bridge::ITimelineController*    timeline{nullptr};
        bridge::IAutomationController*  automation{nullptr};
        bridge::IInputModeController*   inputMode{nullptr};
        bridge::IMeteringProvider*      metering{nullptr};
        bridge::IWaveformCacheProvider* waveform{nullptr};
        bridge::IPatternDataProvider*   patternData{nullptr};
        bridge::IBrowserController*     browser{nullptr};
        bridge::ISessionManager*        sessionManager{nullptr};
        bridge::IProjectLifecycleController* lifecycle{nullptr};
        bridge::IArrangementManagerController* arrangementManager{nullptr};
        bridge::IRenderController*      render{nullptr};
        bridge::IWorkspaceController*   workspace{nullptr};
    };

    explicit PlaylistWindow(const Controllers& ctrl, QWidget* parent = nullptr);
    ~PlaylistWindow() override;

    // ---------------------------------------------------------------------------
    // ISessionChangeListener
    // ---------------------------------------------------------------------------
    /**
     * @brief Called BEFORE the active session is destroyed.
     * Stops the render timer and clears cached widget state.
     */
    void onSessionChanging() override;

    /**
     * @brief Called AFTER the new session is live and wired.
     * Reloads tracks from ITrackController and restarts the 60 Hz timer.
     */
    void onSessionChanged(composition::IProjectSession* newSession) override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    static uint64_t autoSubLaneKey(TrackID id, uint32_t subLaneIndex) {
        return (id.toRaw() << 16) | static_cast<uint64_t>(subLaneIndex);
    }
    // ---------------------------------------------------------------------------
    // Setup Helpers
    // ---------------------------------------------------------------------------
    void setupUI();
    void wireSignals();
    void installKeyboardShortcuts();
    void startRenderLoop();
    void stopRenderLoop();

    /**
     * @brief Helper to recalculate viewport.endFrame based on width, startFrame, and zoomFactor.
     */
    void recalculateViewportEndFrame();

    /**
     * @brief Zoom horizontally by the given multiplier, centering around the viewport center.
     */
    void zoomHorizontal(double zoomScale);

    /**
     * @brief Reset zoom factor to 1.0.
     */
    void zoomReset();

    /**
     * @brief Pushes the current m_viewport to ruler and canvas.
     * Call after any mutation to startFrame, endFrame, zoomFactor, or verticalOffsetPx.
     */
    void applyViewport();

public:
    /**
     * @brief Queries ITrackController::getAllTracks() and refreshes header + canvas.
     * Called from onSessionChanged() and after track add/remove operations.
     */
    void reloadTracks();
    int getMaxVerticalOffset() const;
    void onAnalyzeLoudnessRequested();

private slots:
    // ---------------------------------------------------------------------------
    // 60 Hz Render Tick — the ONLY place bridge state is polled
    // ---------------------------------------------------------------------------
    /**
     * @brief Fires every ~16 ms. Performs (in order):
     *  1. Poll IMeteringProvider::updateMeters(elapsed)
     *  2. Read playhead frame from ITimelineController::getCurrentFrame()
     *  3. Forward meter levels to TrackHeaderView
     *  4. Forward playhead to PlaylistTimelineRuler and PlaylistClipCanvas
     *  5. Trigger repaints if state changed
     */
    void onRenderTick();

    // ---------------------------------------------------------------------------
    // Track mutations (connected from TrackHeaderView signals)
    // ---------------------------------------------------------------------------
    void onAddAudioTrackRequested();
    void onAddInstrumentTrackRequested();
    void onAddAudioTrackWithClipRequested(const QString& absolutePath, uint64_t dropFrame);
    void onAddAudioTracksWithClipsRequested(const QStringList& filePaths, uint64_t dropFrame);
    void onAddInstrumentTrackWithPluginRequested(uint32_t pluginId);
    void onImportAudioFile();
    void onInsertBlankMidiClip();
    
    void onInsertInstrumentRequested(TrackID id, uint32_t pluginId);
    void onInsertPluginRequested(TrackID id, uint32_t pluginId);

    void onTrackMuteToggled(TrackID id, bool mute);
    void onTrackSoloToggled(TrackID id, bool solo);
    void onTrackArmToggled(TrackID id, bool armed);
    void onTrackInputMonitorToggled(TrackID id, bool enabled);
    void onTrackRenameRequested(TrackID id, const QString& newName);
    void onTrackColorChangeRequested(TrackID id, uint32_t colorARGB);
    void onTrackMoveRequested(TrackID id, uint32_t newIndex, TrackID newParentFolderId);
    void onTrackDeleteRequested(TrackID id);
    void onConfigureAutomationRequested(TrackID id);


    // ---------------------------------------------------------------------------
    // Clip / Region mutations (connected from PlaylistClipCanvas signals)
    // ---------------------------------------------------------------------------
    void onRegionMoveRequested(bridge::RegionID id, TrackID destTrack, int64_t newStart, uint32_t destLayer);
    void onRegionSplitRequested(bridge::RegionID id, uint64_t splitFrame);
    void onRegionDeleteRequested(bridge::RegionID id);
    void onRegionGainChanged(bridge::RegionID id, float gainLinear);
    void onRegionFadesChanged(bridge::RegionID id, uint32_t fadeIn, uint32_t fadeOut);

    // ---------------------------------------------------------------------------
    // Viewport / Timeline (connected from ruler / canvas signals)
    // ---------------------------------------------------------------------------
    void onSeekRequested(uint64_t frame);
    void onZoomScrollChanged(uint64_t newStart, double newZoom);
    void onViewportVerticalScrolled(int offsetPx);
    void onTempoCollapseToggled(bool collapsed);
    void onTempoHeightResizeRequested(int newHeight);
    void onSaveProject();
    void onSaveProjectAs();
    void onOpenProject();
    void onExportProjectJson();
    void onImportProjectJson();

Q_SIGNALS:
    void tracksChanged(const std::vector<bridge::TrackUIState>& tracks);
    void trackStatesUpdated(const std::vector<bridge::TrackUIState>& tracks);
    void midiClipDoubleClicked(TrackID trackId, bridge::RegionID regionId);

private:
    Controllers m_ctrl;

    // 60 Hz render loop
    QTimer*        m_renderTimer{nullptr};
    QElapsedTimer  m_elapsedTimer;          ///< Measures actual elapsed ms between ticks

    // Shared view state — propagated to ruler + canvas via applyViewport()
    PlaylistViewport m_viewport;
    uint32_t         m_trackCount{0};
    // Track heights (main lane) map: TrackID (raw) -> height
    std::unordered_map<uint64_t, int> m_trackHeights;
    // Automation heights: SubNodeID (raw) -> height
    std::unordered_map<uint64_t, uint32_t> m_autoSubLaneHeights;
    // Takes lane heights: (TrackID (raw), laneIndex) -> height
    std::map<std::pair<uint64_t, uint32_t>, int> m_takesLaneHeights;
    std::vector<TrackLayout> m_trackLayouts;
    static constexpr int kDefaultTrackHeight = static_cast<int>(bridge::kMainLaneHeightDefault);

    // Cached playhead (updated each tick to detect change before triggering update())
    uint64_t m_lastPlayheadFrame{0};
    double   m_lastBpm{120.0};


    // ---------------------------------------------------------------------------
    // Sub-widgets (created in setupUI, owned by Qt parent–child)
    // ---------------------------------------------------------------------------
    PlaylistMenuButton*    m_menuBtn{nullptr};
    QPushButton*           m_arrangementsBtn{nullptr};
    QMenu*                 m_arrangementsMenu{nullptr};
    QLabel*                m_fpsLabel{nullptr};
    PlaylistTimelineRuler* m_ruler{nullptr};
    TrackHeaderView*       m_trackHeader{nullptr};
    PlaylistClipCanvas*    m_canvas{nullptr};
    PickerPanel*           m_pickerPanel{nullptr};
    MiniPlaylistPreview*   m_preview{nullptr};
    ProjectPickerScreen*   m_projectPicker{nullptr};

    // FPS Monitoring relocated from PlaylistToolBar
    int                    m_frameCount{0};
    QElapsedTimer          m_fpsTimer;
    QTimer                 m_fpsUpdateTimer;

    // Active tool state relocated from PlaylistToolBar
    PlaylistEditTool       m_activeTool{PlaylistEditTool::Select};

    // Arrangements actions management
    void buildArrangementsMenu();
    void onArrangementsMenuTriggered(QAction* action);

    // Event filter for counting paint events (relocated FPS counting logic)
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Tempo automation track components
    TempoTrackHeader*      m_tempoHeader{nullptr};
    TempoTrackCanvas*      m_tempoCanvas{nullptr};
    QWidget*               m_tempoRow{nullptr};
    int                    m_expandedHeight{80};
    bool                   m_tempoCollapsed{true};
};

} // namespace presentation::views
