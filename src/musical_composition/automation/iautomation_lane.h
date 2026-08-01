#pragma once
#include "musical_composition/musical_primitives.h"
#include "common/dsp/curve_interpolation.h"

namespace composition {

using Point = ::AutomationPoint;

enum class AutomationTargetType : uint8_t {
    ChannelStrip = 0, // Volume, Pan, Mute, Solo
    Panner       = 1, // Stereo width, pan mode
    Instrument   = 2, // Track virtual instrument slot
    InsertPlugin = 3, // Inserts (Slots 0-7)
    PreSend      = 4, // Pre-fader sends (Slots 0-3)
    PostSend     = 5, // Post-fader sends (Slots 0-3)
    Unknown      = 255
};

struct AutomationTarget {
    NodeID nodeId;                  // Which node in Layer 4
    uint32_t semanticNameId;        // IStringRegistry ID (e.g., "gain")
    uint32_t cachedParameterIndex;  // Resolved internal DSP index
    uint32_t subNodeId = 0;         // Sub-node ID for plugins
};

struct TargetHash {
    std::size_t operator()(const AutomationTarget& t) const {
        return std::hash<uint64_t>{}( (static_cast<uint64_t>(t.nodeId.generation) << 32) | t.nodeId.id ) ^ 
               std::hash<uint32_t>{}(t.cachedParameterIndex) ^
               std::hash<uint32_t>{}(t.subNodeId);
    }
};

struct TargetEqual {
    bool operator()(const AutomationTarget& a, const AutomationTarget& b) const {
        return a.nodeId == b.nodeId && a.cachedParameterIndex == b.cachedParameterIndex && a.subNodeId == b.subNodeId;
    }
};

class IAutomationLane {
public:
    virtual ~IAutomationLane() = default;

    // CRUD for automation points
    virtual void addPoint(uint64_t samplePosition, float value) = 0;
    virtual void addPoint(uint64_t samplePosition, float value, ::AutomationPoint::Shape shape, float tension) = 0;
    virtual void removePoint(uint64_t samplePosition) = 0;

    /**
     * @brief Erase all points from this lane without pushing any history delta.
     *        Callers are responsible for wrapping this in a compound transaction
     *        if undo support is required.
     */
    virtual void clearPoints() = 0;

    // RT-Safe Evaluation on absolute timeline
    virtual float evaluate(double frame) const = 0;

    // Retrieve points
    virtual uint32_t getPoints(Point* outPoints, uint32_t maxPoints) const = 0;

    // Get Target
    virtual const AutomationTarget& getTarget() const = 0;
};


} // namespace composition
