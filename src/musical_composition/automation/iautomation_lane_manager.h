#pragma once
#include "iautomation_lane.h"

namespace Layer2 { class IEventQueue; }

namespace composition {

class IAutomationLaneManager {
public:
    virtual ~IAutomationLaneManager() = default;

    virtual void setAutomationMode(AutomationMode mode) = 0;
    virtual AutomationMode getAutomationMode() const = 0;
    
    /**
     * @brief Retrieve an existing lane for a target.
     * @thread_safety RT-Safe (no allocation). Returns nullptr if not found.
     */
    virtual IAutomationLane* getLane(const AutomationTarget& target) const = 0;

    /**
     * @brief Create a new lane for a target.
     * @thread_safety NOT RT-Safe (allocates memory).
     */
    virtual IAutomationLane* createLane(const AutomationTarget& target, bool pushDelta = false) = 0;

    /**
     * @brief Remove a lane for a target.
     * @thread_safety NOT RT-Safe.
     */
    virtual void removeLane(const AutomationTarget& target, bool pushDelta = false) = 0;

    /**
     * @brief Mode-guarded addPoint. Returns false without mutating if mode is
     *        OFF or READ. Creates the lane if it does not yet exist.
     *
     * This is the canonical mutation entry point for all NRT write paths
     * (bridge, controller, etc.). Undo/redo and load paths bypass this method
     * and call IAutomationLane::addPoint() directly.
     *
     * @thread_safety NRT only.
     */
    virtual bool addPoint(const AutomationTarget& target,
                          uint64_t samplePosition,
                          float value,
                          ::AutomationPoint::Shape shape = ::AutomationPoint::Shape::LINEAR,
                          float tension = 0.5f) = 0;

    /**
     * @brief Mode-guarded removePoint. Returns false without mutating if mode is
     *        OFF or READ. Returns false if the target lane does not exist.
     *
     * @thread_safety NRT only.
     */
    virtual bool removePoint(const AutomationTarget& target,
                             uint64_t samplePosition) = 0;

    /**
     * @brief Edits the shape and tension of an existing point. Mode-guarded.
     * @thread_safety NRT only.
     */
    virtual void editPointShapeAndTension(TrackID trackId, NodeID nodeId, uint32_t subNodeId, uint32_t parameterIndex, uint32_t pointIndex, uint8_t shape, float tension) = 0;


    /**
     * @brief Evaluates all lanes and pushes parameter change events.
     * @thread_safety RT-Safe but NOT thread-safe for concurrent lane creation.
     * @param startSample Start position of the current processing block
     * @param numSamples Length of the block
     * @param eventQueue Layer 3 queue for DSP consumption
     */
    virtual void renderToEvents(
        uint64_t startSample,
        uint32_t numSamples,
        bool loopEnabled,
        uint64_t loopStart,
        uint64_t loopEnd,
        Layer2::IEventQueue* eventQueue
    ) const = 0;
};

} // namespace composition
