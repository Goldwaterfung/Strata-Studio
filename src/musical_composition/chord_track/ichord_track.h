#pragma once
#include "musical_composition/musical_primitives.h"

namespace composition {

enum class ScaleType : uint8_t {
    MAJOR, MINOR, HARMONIC_MINOR, MELODIC_MINOR, DORIAN, MIXOLYDIAN,
};

enum class ChordQuality : uint8_t {
    MAJOR, MINOR, DIMINISHED, AUGMENTED, DOMINANT_7, MAJOR_7, MINOR_7,
};

struct ChordEvent {
    ChordID id;
    uint64_t positionSample;
    uint8_t rootNote;
    uint8_t bassNote;
    ChordQuality quality;       
    ScaleType activeScale;
};

class IChordTrack {
public:
    virtual ~IChordTrack() = default;

    virtual ChordID addChord(const ChordEvent& chord) = 0;
    virtual void removeChord(ChordID id) = 0;
    virtual void updateChord(ChordID id, const ChordEvent& newChord) = 0;

    virtual bool getActiveChordAt(uint64_t samplePosition, ChordEvent& outChord) const = 0;
};

} // namespace composition
