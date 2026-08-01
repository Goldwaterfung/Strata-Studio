// src/Presentation/views/playlist/PlaylistEditTool.h
#pragma once
#include <cstdint>

namespace presentation::views {

/**
 * @brief Active editing tool for the playlist canvas.
 */
enum class PlaylistEditTool : uint8_t {
    Draw,         ///< Draw clips (pencil)
    Paint,        ///< Repeat/paint clips (brush)
    Delete,       ///< Erase clips (eraser)
    Mute,         ///< Toggle clip muting
    SlipEdit,     ///< Adjust slip offsets within clip
    Slice,        ///< Cut/split clips
    Select,       ///< Multi-select pointer
    ZoomSelect,   ///< Zoom window selector
    PlaySelected  ///< Audition/play clicked clip
};

} // namespace presentation::views
