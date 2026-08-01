// src/Presentation/views/playlist/PlaylistContextMenu.h
#pragma once

#include <QMenu>
#include <vector>
#include "timeline/iarrangement_controller.h"
#include "tracks/itrack_controller.h"
#include "PlaylistEditTool.h" // for PlaylistEditTool

namespace presentation::views {

/**
 * @brief Dynamic right-click context menu factory for the playlist clip canvas.
 *
 * Adheres strictly to Phase 9. Provides:
 *  - Custom cyberpunk visual style.
 *  - Grouped actions tailored to clicked elements (clips or empty lane grids).
 *  - Inline custom styled spinners/input prompts for parameters like gain and fades.
 *  - Clean routing of all execution states directly to middle bridge controllers.
 */
class PlaylistContextMenu {
public:
    /**
     * @brief Build a context menu for right-clicking an active region/clip.
     * Contains split, delete, mute, gain, fade in/out config, and move-to-track submenus.
     */
    static QMenu* buildForRegion(
        const bridge::VisualRegion& region,
        PlaylistEditTool activeTool,
        uint64_t frameAtClick,
        const std::vector<bridge::TrackUIState>& tracks,
        bridge::IArrangementController* arrangement,
        QWidget* parent);

    /**
     * @brief Build a context menu for right-clicking empty space in track lanes.
     * Contains audio clip importing and automation curve insertion.
     */
    static QMenu* buildForArrangementLane(
        TrackID trackId,
        composition::TrackType trackType,
        uint64_t frameAtClick,
        bridge::IArrangementController* arrangement,
        QWidget* parent);

    static QMenu* buildForAutomationSegment(
        const bridge::VisualAutomationPoint& leftPoint,
        std::function<void(uint8_t)> onShapeChanged,
        QWidget* parent);

    static QMenu* buildForAutomationSubLane(
        bool hasHighlight,
        std::function<void()> onCopyPoints,
        QWidget* parent);

};

} // namespace presentation::views
