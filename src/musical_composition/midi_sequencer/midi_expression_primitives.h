// src/musical_composition/midi_sequencer/midi_expression_primitives.h
#pragma once
#include <cstdint>

namespace composition {

// Continuous Controller automation point primitive
struct MIDICCPoint {
    uint64_t absoluteTickPosition; // Absolute musical position (ticks from project start)
    uint64_t samplePosition;     // Absolute session playhead position in samples
    uint8_t channel;             // MIDI Channel (0-15)
    uint8_t controllerNumber;    // VST3/MIDI CC Number (e.g. 1 = Mod Wheel, 11 = Expression)
    uint8_t value;               // CC Value (0-127)
    uint8_t padding[5];          // Data structure alignment padding
};

// 14-bit pitch bend point primitive
struct MIDIPitchPoint {
    uint64_t absoluteTickPosition; // Absolute musical position (ticks from project start)
    uint64_t samplePosition;     // Absolute session playhead position in samples
    uint8_t channel;             // MIDI Channel (0-15)
    uint16_t value;              // 14-bit high-resolution value (0-16383, 8192 = Center)
    uint8_t padding[5];          // Data structure alignment padding
};

} // namespace composition
