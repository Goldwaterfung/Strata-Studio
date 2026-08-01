// src/Middle Bridge/midi_clip_primitives.h
#pragma once
#include "common/system_primitives.h"
#include "musical_composition/musical_primitives.h"

namespace bridge {

using composition::RegionID;
using composition::NoteID;

constexpr uint32_t MAX_MIDI_NOTES_VIEWPORT = 1024;
constexpr uint32_t MAX_CC_POINTS_VIEWPORT  = 512;


/// POD snapshot of a single CC lane's value points for the Piano Roll expression lane
struct VisualCCPoint {
    uint64_t framePosition;
    uint8_t  controllerNumber;
    uint8_t  value;           ///< 0-127
    uint8_t  _pad[6];
};
static_assert(std::is_trivially_copyable<VisualCCPoint>::value, "VisualCCPoint must be trivially copyable");

/// Piano Roll edit mode discriminator (POD-safe enum)
enum class PianoRollTool : uint8_t {
    DRAW   = 0,   ///< Left-click draws/erases notes
    SELECT = 1,   ///< Rubber-band selection
    ERASE  = 2,   ///< Left-click deletes notes
    ZOOM   = 3,   ///< Scroll-wheel zoom only
};

} // namespace bridge
