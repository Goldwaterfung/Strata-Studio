#pragma once
#include <cstdint>
#include "musical_composition/musical_primitives.h"

namespace composition {

namespace MidiOps {
    constexpr uint32_t ADD_NOTE    = 0;
    constexpr uint32_t REMOVE_NOTE = 1;
    constexpr uint32_t UPDATE_NOTE = 2;
    constexpr uint32_t ADD_CC      = 3;
    constexpr uint32_t REMOVE_CC   = 4;
    constexpr uint32_t UPDATE_CC   = 5;
    constexpr uint32_t ADD_PITCH   = 6;
    constexpr uint32_t REMOVE_PITCH = 7;
    constexpr uint32_t UPDATE_PITCH = 8;
}

struct AddNotePayload {
    ClipID clipId;
    MIDINote note;
};

struct AddCCPayload {
    ClipID clipId;
    MIDICCPoint point;
};

struct AddPitchPayload {
    ClipID clipId;
    MIDIPitchPoint point;
};

static_assert(sizeof(AddNotePayload) <= 256, "Add note payload too large");
static_assert(sizeof(AddCCPayload) <= 256, "Add CC payload too large");
static_assert(sizeof(AddPitchPayload) <= 256, "Add Pitch payload too large");

} // namespace composition
