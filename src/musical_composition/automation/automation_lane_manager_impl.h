#pragma once
#include "iautomation_lane_manager.h"
#include "musical_composition/command_history/delta_primitives.h"
#include <unordered_map>
#include <memory>

namespace composition {

class ICommandHistory;

class AutomationLaneManagerImpl : public IAutomationLaneManager {
public:
    AutomationLaneManagerImpl(TrackID trackId, ICommandHistory* history);

    void setAutomationMode(AutomationMode mode) override { mode_ = mode; }
    AutomationMode getAutomationMode() const override { return mode_; }
    TrackID getTrackId() const { return trackId_; }
    
    IAutomationLane* getLane(const AutomationTarget& target) const override;
    IAutomationLane* createLane(const AutomationTarget& target, bool pushDelta = false) override;
    bool addPoint(const AutomationTarget& target,
                  uint64_t samplePosition,
                  float value,
                  ::AutomationPoint::Shape shape = ::AutomationPoint::Shape::LINEAR,
                  float tension = 0.5f) override;
    bool removePoint(const AutomationTarget& target, uint64_t samplePosition) override;
    void editPointShapeAndTension(TrackID trackId, NodeID nodeId, uint32_t subNodeId, uint32_t parameterIndex, uint32_t pointIndex, uint8_t shape, float tension) override;
    void removeLane(const AutomationTarget& target, bool pushDelta = false) override;

    void renderToEvents(uint64_t startSample, uint32_t numSamples, bool loopEnabled, uint64_t loopStart, uint64_t loopEnd, Layer2::IEventQueue* eventQueue) const override;

    void applyDelta(const ProjectDelta& delta, bool isUndo);
    void copyFrom(const AutomationLaneManagerImpl* other);

    const std::unordered_map<AutomationTarget, std::unique_ptr<IAutomationLane>, TargetHash, TargetEqual>& getLanes() const { return lanes_; }

private:
    TrackID trackId_;
    ICommandHistory* history_;
    AutomationMode mode_ = AutomationMode::READ;

    std::unordered_map<AutomationTarget, std::unique_ptr<IAutomationLane>, TargetHash, TargetEqual> lanes_;
};

} // namespace composition
