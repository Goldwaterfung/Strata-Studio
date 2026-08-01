// src/Presentation/views/playlist/PlaylistViewport.h
#pragma once

#include <cstdint>

namespace presentation::views {

/**
 * @brief Shared scroll / zoom state object owned by PlaylistWindow.
 *
 * PlaylistWindow holds a single PlaylistViewport instance and propagates
 * it to PlaylistTimelineRuler and PlaylistClipCanvas via applyViewport()
 * whenever any field is mutated (wheel scroll, zoom button, ruler edge-drag).
 *
 * POD-safe: trivially copyable, no heap allocation.
 */
struct PlaylistViewport {
    uint64_t startFrame{0};       ///< Leftmost visible frame
    uint64_t endFrame{0};         ///< Rightmost visible frame
    double   zoomFactor{0.001};   ///< Pixels per frame (positive)
    int      verticalOffsetPx{0}; ///< Track scroll offset in pixels
};

} // namespace presentation::views
