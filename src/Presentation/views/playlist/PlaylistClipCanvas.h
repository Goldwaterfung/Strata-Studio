// src/Presentation/views/playlist/PlaylistClipCanvas.h
#pragma once

#include <QWidget>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <QTimer>

// Bridge interfaces (Layer 7 may only import from Middle Bridge)
#include "timeline/iarrangement_controller.h"
#include "TrackLayout.h"
#include "Middle Bridge/automation/automation_helpers.h"
#include "timeline/itimeline_controller.h"
#include "telemetry/iwaveform_cache_provider.h"
#include "automation/iautomation_controller.h"
#include "telemetry/ipattern_data_provider.h"
#include "browser/ibrowser_controller.h"
#include "engine/iinput_mode_controller.h"
#include "clips/AudioClipItem.h"
#include "playlist/waveform/WaveformTileCache.h"

class QMimeData;

namespace presentation::views {

class TileRenderWorker;

/**
 * @brief 9-state drag enum for the clip canvas interaction state machine.
 *
 * All state transitions happen in mousePressEvent / mouseMoveEvent /
 * mouseReleaseEvent only. The enum is uint8_t to keep the size minimal.
 */
enum class CanvasDragState : uint8_t {
    Idle,
    DraggingClip,           ///< Moving a region to a new track/position
    ResizingClipRight,      ///< Extending / shrinking the right edge
    ResizingClipLeft,       ///< Slip-edit the left edge
    DraggingFadeIn,         ///< Adjusting the fade-in handle
    DraggingFadeOut,        ///< Adjusting the fade-out handle
    DraggingGainBadge,      ///< Vertical drag on the gain badge
    DrawingNewClip,         ///< Draw/Paint tool — rubber-band a new region
    SelectionRubberBand,    ///< Select tool — rubber-band multi-select
    DraggingControlPoint,   ///< Automation tool — moving a VisualAutomationPoint
    DraggingAutomationTension, ///< Alt+Drag on curve segment to adjust tension
    CompHighlighting,       ///< Dragging a time slice highlight for comping
};

/**
 * @brief Core arrangement grid widget for the Playlist view.
 *
 * Responsibilities:
 *  - Renders all clip types (Audio, Pattern, Automation) via stateless
 *    ClipItem paint helpers. Zero heap allocation inside paintEvent.
 *  - Maintains a ViewState that is pushed from PlaylistWindow via setViewState().
 *  - Implements a 9-state drag-state machine for all mouse interactions.
 *  - Emits mutation signals; PlaylistWindow translates them to bridge calls.
 *
 * Threading discipline:
 *  - paintEvent reads only member variables — never calls bridge methods.
 *  - Bridge data is fetched inside paintEvent via getRegionsInViewport()
 *    using a stack-allocated VisualRegion[MAX_VISIBLE] buffer.
 *  - setPlayheadFrame() is called from the 60 Hz render tick slot.
 */
class PlaylistClipCanvas : public QWidget {
    Q_OBJECT

public:
    // -------------------------------------------------------------------------
    // Shared view-state aggregate pushed from PlaylistWindow
    // -------------------------------------------------------------------------
    struct ViewState {
        uint64_t viewStartFrame{0};
        uint64_t viewEndFrame{0};
        double   zoomFactor{0.001};   ///< Pixels per frame (positive)
        uint32_t trackCount{0};
        std::vector<TrackLayout> trackLayouts;
        int defaultTrackHeight{static_cast<int>(bridge::kMainLaneHeightDefault)};
        int verticalOffsetPx{0};    ///< Track scroll offset in pixels
    };

    explicit PlaylistClipCanvas(
        bridge::IArrangementController* arrangement,
        bridge::ITimelineController*    timeline,
        bridge::IWaveformCacheProvider* waveform,
        bridge::IAutomationController*  automation,
        bridge::IPatternDataProvider*   patternData,
        bridge::IBrowserController*     browser,
        bridge::IInputModeController*   inputMode,
        QWidget* parent = nullptr);

    ~PlaylistClipCanvas() override = default;

    // -------------------------------------------------------------------------
    // State setters (called from PlaylistWindow, 60 Hz tick, or ruler)
    // -------------------------------------------------------------------------

    /**
     * @brief Push a new viewport / track-layout state.
     *        Triggers update() if state changed.
     */
    void setViewState(const ViewState& vs);

    // Comp Highlight API
    bool hasCompHighlight() const { 
        // Allow end < start (user dragging left): drawCompHighlight() normalises
        // the range itself with std::min/std::max before drawing.
        // Allow lane == 0 (main lane) and automation sub-lanes.
        // drawCompHighlight() resolves the correct Y from m_compHighlightClipType.
        return m_compHighlightLane >= 0 && m_compHighlightEndFrame != m_compHighlightStartFrame; 
    }
    TrackID getCompHighlightTrack() const { return m_compHighlightTrack; }
    int getCompHighlightLane() const { return m_compHighlightLane; }
    uint64_t getCompHighlightStart() const { return std::min(m_compHighlightStartFrame, m_compHighlightEndFrame); }
    uint64_t getCompHighlightLength() const { 
        const uint64_t lo = std::min(m_compHighlightStartFrame, m_compHighlightEndFrame);
        const uint64_t hi = std::max(m_compHighlightStartFrame, m_compHighlightEndFrame);
        return hi - lo;
    }
    void clearCompHighlight() {
        m_compHighlightStartFrame = 0;
        m_compHighlightEndFrame = 0;
        m_compHighlightLane = -1;
        m_compHighlightClipType = composition::RegionType::AUDIO;
        m_compHighlightNodeId = NodeID::invalid();
        m_compHighlightParamIndex = 0;
        update();
    }

    /**
     * @brief Cache the track list so paintEvent can map regionId → row.
     */
    void setTrackList(const std::vector<bridge::TrackUIState>& tracks);

    /**
     * @brief Invalidate cache for a specific media ID.
     */
    void invalidateMedia(MediaID id) { m_tileCache.invalidateMedia(id); }

    /**
     * @brief Update playhead position. Calls update() only when pixel position
     *        changed to avoid redundant repaints.
     */
    void setPlayheadFrame(uint64_t frame);

    /**
     * @brief Clear all cached state — called from onSessionChanging().
     */
    void clearAll();

    // -------------------------------------------------------------------------
    // Focus Bar Setters
    // -------------------------------------------------------------------------

    void setShowFades(bool on) {
        if (m_showFades != on) {
            m_showFades = on;
            update();
        }
    }
    bool showFades() const { return m_showFades; }

    void setStretchMode(bool on) {
        if (m_stretchMode != on) {
            m_stretchMode = on;
            update();
        }
    }
    bool stretchMode() const { return m_stretchMode; }
    void setActiveDragType(int type) {
        if (m_activeDragType != type) {
            m_activeDragType = type;
            update();
        }
    }
    void clearActiveDragType() {
        if (m_activeDragType != -1) {
            m_activeDragType = -1;
            update();
        }
    }

    const std::unordered_set<uint64_t>& getSelectedRegions() const { return m_selectedRegions; }
    void selectRegions(const std::vector<bridge::RegionID>& ids);

    // -------------------------------------------------------------------------
    // Editing Operations (shortcuts & menu triggers)
    // -------------------------------------------------------------------------
    void splitClipsAtPlayhead(uint64_t playheadFrame);
    void deleteSelectedClips();
    void duplicateSelectedClips();
    void toggleMuteSelectedClips();
    void quantizeSelectedClips();
    void consolidateSelectedClips();
    bool getSelectedRange(uint64_t& outStartFrame, uint64_t& outEndFrame) const;
    void nudgeSelectedClips(int64_t deltaFrames);
    void moveSelectedClipsTrack(int trackOffset);
    void renameSelectedClip();
    void openFadeConfigDialog();
    void invertSelection();

public slots:
    void selectAll();
    void deselectAll();
    void onTileRendered(presentation::views::TileKey key, QImage image);

signals:
    // -------------------------------------------------------------------------
    // Mutation signals — PlaylistWindow connects these to bridge calls
    // -------------------------------------------------------------------------
    void regionMoveRequested(bridge::RegionID id, TrackID destTrack, int64_t newStart, uint32_t destLayer);
    void regionSplitRequested(bridge::RegionID id, uint64_t splitFrame);
    void regionDeleteRequested(bridge::RegionID id);
    void regionGainChanged(bridge::RegionID id, float gainLinear);
    void regionFadesChanged(bridge::RegionID id, uint32_t fadeIn, uint32_t fadeOut);
    void regionStretchRequested(bridge::RegionID id, double ratio);
    void regionTrimRequested(bridge::RegionID id, uint64_t newPosition, uint64_t newSourceStart, uint64_t newDuration);
    void midiClipDoubleClicked(TrackID trackId, bridge::RegionID regionId);
    void trackSelectionRequested(TrackID trackId, bool multiSelect, bool rangeSelect);

    // -------------------------------------------------------------------------
    // DnD new track & plugin signals
    // -------------------------------------------------------------------------
    void addAudioTrackWithClipRequested(const QString& filePath, uint64_t dropFrame);
    void addAudioTracksWithClipsRequested(const QStringList& filePaths, uint64_t dropFrame);
    void addInstrumentTrackWithPluginRequested(uint32_t pluginId);
    void insertInstrumentRequested(TrackID trackId, uint32_t pluginId);
    void insertPluginRequested(TrackID trackId, uint32_t pluginId);

protected:
    // -------------------------------------------------------------------------
    // Qt event overrides
    // -------------------------------------------------------------------------
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    uint32_t resolvePluginIdFromMime(const QMimeData* mime) const;
    void prefetchWaveformTiles();

    // -------------------------------------------------------------------------
    // Paint helpers (read member vars only — no bridge calls)
    // -------------------------------------------------------------------------
    void drawBackground(QPainter& p);
    void drawTrackLanes(QPainter& p);
    void drawGridLines(QPainter& p);
    void drawPlayhead(QPainter& p);
    void drawRubberBand(QPainter& p);
    void drawCompHighlight(QPainter& p);
    void drawHUD(QPainter& p);  ///< Bug 4: precision value tooltip overlay

    // -------------------------------------------------------------------------
    // Geometry helpers
    // -------------------------------------------------------------------------

    /**
     * @brief Get the cumulative Y offset for a track row (sum of heights above it).
     */
    double getTrackYOffset(int trackIndex) const;

    /**
     * @brief Convert a VisualRegion to its screen QRectF using m_view.
     *        Pure function — no bridge access. All math in double precision.
     */
    QRectF regionToRect(const bridge::VisualRegion& region) const;

    /**
     * @brief Map a Y pixel coordinate to the track index it falls on.
     *        Returns -1 if outside all track rows.
     */
    int yToTrackIndex(double y) const;
    void yToTrackAndLayer(double y, int& outTrackIndex, uint32_t& outLayer) const;

    /**
     * @brief Map a X pixel coordinate to the frame position.
     */
    uint64_t xToFrame(double x) const;

    /**
     * @brief Map a frame position to X pixel coordinate.
     */
    double frameToX(uint64_t frame) const;

    /**
     * @brief Snap a frame value to the current active grid setting.
     * @param roundDir: 0=nearest, 1=ceil, -1=floor
     */
    uint64_t snapFrame(uint64_t frame, int roundDir = 0) const;

    /**
     * @brief Calculate the playhead X position in pixels.
     *        Returns < 0 if playhead is outside the visible range.
     */
    double playheadX() const;
    void splitClipsInHighlight();
    void deleteClipsInHighlight();



    // -------------------------------------------------------------------------
    // Hit-testing (used in mousePressEvent)
    // -------------------------------------------------------------------------

    /**
     * @brief Hit-test a point against all visible regions.
     *        Priority order (plan §Phase 2-E):
     *          1. fade-in handle  2. fade-out handle  3. resize-right
     *          4. resize-left     5. gain badge        6. automation point
     *          7. clip body       8. empty lane
     * @param pos       Mouse position in widget coordinates
     * @param regions   Currently visible regions array
     * @param count     Number of valid entries in regions
     * @param outState  Set to the appropriate drag state on hit
     * @return index into regions[] of the hit region, or -1 for empty lane
     */
    int hitTest(const QPointF& pos,
                const bridge::VisualRegion* regions, uint32_t count,
                CanvasDragState& outState,
                int* outPointIndex = nullptr) const;

    // -------------------------------------------------------------------------
    // Bridge interfaces (never called from paintEvent)
    // -------------------------------------------------------------------------
    bridge::IArrangementController* m_arrangement{nullptr};
    bridge::ITimelineController*    m_timeline{nullptr};
    bridge::IWaveformCacheProvider* m_waveform{nullptr};
    bridge::IAutomationController*  m_automation{nullptr};
    bridge::IPatternDataProvider*   m_patternData{nullptr};
    bridge::IBrowserController*     m_browser{nullptr};
    bridge::IInputModeController*   m_inputMode{nullptr};

    // -------------------------------------------------------------------------
    // Cached view state (written by setViewState / setTrackList)
    // -------------------------------------------------------------------------
    ViewState                          m_view;
    std::vector<bridge::TrackUIState>  m_tracks;    ///< Track order for row mapping
    uint64_t                           m_playheadFrame{0};
    double                             m_cachedPlayheadX{-1.0}; ///< Pixels; <0 = off-screen

    // -------------------------------------------------------------------------
    // Drag state machine
    // -------------------------------------------------------------------------
    CanvasDragState  m_dragState{CanvasDragState::Idle};
    bridge::RegionID m_dragRegionId;
    TrackID          m_dragOrigTrackId;
    int             m_dragOrigTrackIndex{-1};
    uint32_t        m_dragOrigLayerIndex{0xFFFFFFFF};

    /// Snap-to-track index during a clip drag (-1 = same track)
    int             m_dragDestTrackIndex{-1};
    uint32_t        m_dragDestLayerIndex{0xFFFFFFFF};
    bool            m_dragHoveringEmptySpace{false};
    uint64_t        m_dragStartFrame{0};   ///< Frame under cursor at press
    uint64_t        m_dragOrigStartFrame{0}; ///< Clip's original startFrame at press
    uint64_t        m_dragOrigSourceStart{0}; ///< Clip's original fileOffsetFrames at press
    uint64_t        m_dragOrigDuration{0}; ///< Clip's original durationFrames at press
    uint64_t        m_dragOrigSourceLength{~0ULL}; ///< Clip's original sourceLengthFrames at press
    double          m_dragOrigRatio{1.0};  ///< Clip's original playback speed ratio at press
    QPointF         m_dragAnchorPos;       ///< Widget-space press position

    /// Frame delta accumulated during drag (live preview, committed on release)
    int64_t         m_dragFrameDelta{0};

    /// Gain delta accumulated during DraggingGainBadge vertical drag
    float           m_dragGainDelta{0.0f};
    float           m_dragOrigGain{1.0f};  ///< Gain at press time

    /// Fade frame delta during DraggingFadeIn/FadeOut
    int64_t         m_dragFadeDelta{0};
    uint32_t        m_dragOrigFadeIn{0};
    uint32_t        m_dragOrigFadeOut{0};

    /// Automation point dragging state
    int             m_dragPointIndex{-1};
    uint64_t        m_dragOrigPointFrame{0};
    float           m_dragOrigPointValue{0.0f};
    float           m_dragOrigTension{0.0f};
    uint32_t        m_dragActualPointIndex{0};
    uint8_t         m_dragCurveShape{0};

    /// Bug 4: HUD tooltip overlay state (set while DraggingControlPoint)
    bool            m_hudVisible{false};
    QPointF         m_hudPos;
    QString         m_hudText;

    /// Rubber-band selection rectangle (SelectionRubberBand / DrawingNewClip)
    QRectF          m_rubberBandRect;
    bool            m_rubberBandVisible{false};

    /// Comping Highlight state
    uint64_t                    m_compHighlightStartFrame{0};
    uint64_t                    m_compHighlightEndFrame{0};
    int                         m_compHighlightLane{-1};
    TrackID                     m_compHighlightTrack{0, 0};
    composition::RegionType     m_compHighlightClipType{composition::RegionType::AUDIO};
    NodeID                      m_compHighlightNodeId{NodeID::invalid()};
    uint32_t                    m_compHighlightParamIndex{0};

    // -------------------------------------------------------------------------
    // Hit-test geometry constants
    // -------------------------------------------------------------------------
    static constexpr double FADE_HANDLE_PX     = 8.0;  ///< Fade handle zone width
    static constexpr double RESIZE_HANDLE_PX   = 6.0;  ///< Resize zone width
    static constexpr double GAIN_BADGE_H_PX    = 14.0; ///< Gain badge height
    static constexpr double GAIN_BADGE_W_PX    = 32.0; ///< Gain badge width

    // paintEvent stack buffer sizes (must be compile-time constants)
    static constexpr uint32_t MAX_VISIBLE = 512;


    bool          m_showFades{true};
    bool          m_stretchMode{false};
    int           m_noteColorMode{0};
    int           m_activeDragType{-1};
    std::unordered_set<uint64_t> m_selectedRegions;
    std::unordered_set<uint64_t> m_initialSelectedRegions;
    bridge::RegionID m_lastSelectedRegionId{bridge::RegionID::invalid()};

    WaveformTileCache m_tileCache;
    std::unique_ptr<TileRenderWorker> m_tileWorker;
    std::unordered_set<TileKey> m_inFlightTiles;
    QTimer m_retryTimer;
    QTimer m_heightDebounceTimer;
};

} // namespace presentation::views
