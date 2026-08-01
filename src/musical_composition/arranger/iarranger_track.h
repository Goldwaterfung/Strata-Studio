#pragma once
#include "musical_composition/musical_primitives.h"

namespace composition {

enum class SectionType : uint8_t {
    CUSTOM = 0, INTRO = 1, VERSE = 2, PRECHORUS = 3, 
    CHORUS = 4, BRIDGE = 5, OUTRO = 6, SOLO = 7
};

struct Section {
    uint32_t nameId;            // IStringRegistry ID
    MusicalPosition startPosition;
    MusicalPosition endPosition;
    uint64_t startSample;       // Absolute section start
    uint64_t lengthSamples;     // Absolute section length
    uint32_t repeatCount;       // Number of times this section loops
    uint32_t colorARGB;         // UI Display color
    bool isMuted;               // Skips this section during playback
    SectionType type;           // Semantic type
};

class IArrangerTrack {
public:
    virtual ~IArrangerTrack() = default;

    virtual void addSection(const Section& section) = 0;
    virtual void removeSection(uint64_t startSample) = 0;
    
    virtual bool getSectionAt(uint64_t samplePosition, Section& outSection) const = 0;
    virtual uint32_t getAllSections(Section* outSections, uint32_t maxSections) const = 0;
};

} // namespace composition
