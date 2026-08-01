#pragma once
#include "musical_composition/musical_primitives.h"
#include <vector>

namespace composition {

enum class ClipboardPayloadType : uint8_t {
    AUDIO_REGION,
    MIDI_NOTE,
    AUTOMATION_POINT,
    TRACK_SETTINGS
};

struct ClipboardItem {
    ClipboardPayloadType type;
    uint64_t relativeSamplePosition; 
    
    union Payload {
        TimelineRegion regionData;
        MIDINote noteData;
        AutomationPoint automationData;
    } data;
};

class IClipboard {
public:
    virtual ~IClipboard() = default;

    virtual void clear() = 0;
    virtual void addItem(const ClipboardItem& item) = 0;
    virtual const std::vector<ClipboardItem>& getItems() const = 0;
    virtual bool isEmpty() const = 0;
};

} // namespace composition
