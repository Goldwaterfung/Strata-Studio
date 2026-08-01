// src/Presentation/views/mixer/mixer_window.h
#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "channel_strip_widget.h"
#include "Middle Bridge/tracks/itrack_controller.h"
#include "Middle Bridge/telemetry/imetering_provider.h"
#include "../playlist/TrackHeaderView.h"

namespace presentation::views {

/**
 * @brief The main Mixer Window component.
 *
 * Layout (horizontal):
 *  ┌─────────────────────────────────────────────┬────────────────────┐
 *  │  [Options Bar: "⚙ Show Sends" toggle]        │                    │
 *  ├─────────────────────────────────────────────┤                    │
 *  │  Scrollable MixerMultiChannelContainer       │  Master Strip      │
 *  │  [Track1][Track2][Track3]...[+Add Track]     │  (fixed, anchored) │
 *  └─────────────────────────────────────────────┴────────────────────┘
 *
 * The window loads all tracks from ITrackController::getAllTracks() on
 * construction and rebuilds when tracks are added or removed.
 *
 * Layer 7 constraint: all communication goes through bridge::ITrackController
 *                     and bridge::IMeteringProvider exclusively.
 */
class MixerWindow : public QWidget {
    Q_OBJECT

public:
    explicit MixerWindow(QWidget* parent = nullptr);
    ~MixerWindow() override = default;

    /**
     * @brief Binds the mixer to the Middle Bridge facades.
     *        Must be called once before the window is shown.
     */
    void bind(bridge::ITrackController* controller,
              bridge::IMeteringProvider* meteringProvider,
              bridge::IAutomationController* automation);

    /**
     * @brief Rebuild the channel strip list from the current track list.
     *        Call whenever tracks are added, removed, or reordered.
     */
    void rebuildStrips();

    /**
     * @brief Handle a track dropped onto the mixer.
     */
    void handleTrackDrop(TrackID draggedId, TrackID targetId, presentation::views::DropAction action);

    /**
     * @brief Refresh all strips from the latest bridge state.
     *        Called by the PresentationDirector at 60 Hz.
     */
    void updateFromBridge();

    /**
     * @brief Update the full UI state of existing strips in-place.
     */
    void updateTrackStates(const std::vector<bridge::TrackUIState>& tracks);

    /**
     * @brief Exposes the active track channel strips.
     */
    const std::vector<ChannelStripWidget*>& trackStrips() const { return m_strips; }

    /**
     * @brief Exposes the master channel strip.
     */
    ChannelStripWidget* masterStrip() const { return m_masterStrip; }

    bridge::ITrackController* controller() const { return m_controller; }

    ChannelStripWidget* strip(TrackID trackId) const {
        auto it = m_stripMap.find(trackId.toRaw());
        return it != m_stripMap.end() ? it->second : nullptr;
    }

public Q_SLOTS:
    void toggleSendsVisible(bool visible);
    void drawFolderTrays(QPainter& p);

protected:
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

Q_SIGNALS:
    void trackMoveRequested(TrackID id, uint32_t newIndex, TrackID newParentFolderId);
    void tracksChangedRequested();
    void closedByUser();

private Q_SLOTS:
    void onFolderCollapseToggled(TrackID folderId, bool collapsed);
    void onSelectionRequested(TrackID trackId, bool multiSelect, bool rangeSelect);

private:
    void buildLayout();
    void updateMinimumHeight();
    void installKeyboardShortcuts();

    int calculateTrackDepth(TrackID id, const std::vector<bridge::TrackUIState>& tracks) const;
    TrackID getParentFolderId(TrackID id, const std::vector<bridge::TrackUIState>& tracks) const;
    bool isDescendantOf(TrackID childId, TrackID parentId, const std::vector<bridge::TrackUIState>& tracks) const;
    bool isTrackVisible(TrackID id, const std::vector<bridge::TrackUIState>& tracks) const;

    // --- Bridge ---
    bridge::ITrackController*  m_controller       = nullptr;
    bridge::IMeteringProvider* m_meteringProvider = nullptr;
    bridge::IAutomationController* m_automation   = nullptr;

    // --- Layout Skeleton ---
    QVBoxLayout*  m_rootLayout     = nullptr;
    QWidget*      m_optionsBar     = nullptr;
    QPushButton*  m_sendsToggleBtn = nullptr;
    QScrollArea*  m_scrollArea     = nullptr;
    QWidget*      m_scrollContent  = nullptr;
    QHBoxLayout*  m_stripLayout    = nullptr;
    QFrame*       m_divider        = nullptr;
    ChannelStripWidget* m_masterStrip = nullptr;

    // --- Track strips ---
    std::vector<ChannelStripWidget*> m_strips;
    std::unordered_map<uint64_t, ChannelStripWidget*> m_stripMap;
    std::unordered_set<uint64_t> m_collapsedFolders;
    bool m_sendsVisible = true;
    int m_lastSelectedTrackIndex = -1;
};

} // namespace presentation::views
