#include "automation_lane_manager_impl.h"
#include "automation_lane_impl.h"
#include "automation_commands.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include "musical_composition/command_history/icommand_history.h"

namespace composition {

AutomationLaneManagerImpl::AutomationLaneManagerImpl(TrackID trackId, ICommandHistory* history)
    : trackId_(trackId), history_(history) {}

IAutomationLane* AutomationLaneManagerImpl::getLane(const AutomationTarget& target) const {
    auto it = lanes_.find(target);
    if (it != lanes_.end()) return it->second.get();
    return nullptr;
}

IAutomationLane* AutomationLaneManagerImpl::createLane(const AutomationTarget& target, bool pushDelta) {
    auto it = lanes_.find(target);
    if (it != lanes_.end()) return it->second.get();
    
    auto lane = std::make_unique<AutomationLaneImpl>(target, history_);
    IAutomationLane* ptr = lane.get();
    lanes_[target] = std::move(lane);

    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::AUTOMATION;
        delta.operationType = AutomationOps::CREATE_LANE;
        delta.targetId = trackId_.toRaw();
        AutomationLanePayload payload{ target };
        delta.newStateSize = sizeof(AutomationLanePayload);
        std::memcpy(delta.newState, &payload, sizeof(AutomationLanePayload));
        history_->pushDelta(delta);
    }

    return ptr;
}

void AutomationLaneManagerImpl::removeLane(const AutomationTarget& target, bool pushDelta) {
    auto it = lanes_.find(target);
    if (it != lanes_.end()) {
        if (pushDelta && history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::AUTOMATION;
            delta.operationType = AutomationOps::REMOVE_LANE;
            delta.targetId = trackId_.toRaw();
            AutomationLanePayload payload{ target };
            delta.oldStateSize = sizeof(AutomationLanePayload);
            std::memcpy(delta.oldState, &payload, sizeof(AutomationLanePayload));
            history_->pushDelta(delta);
        }
        lanes_.erase(it);
    }
}

void AutomationLaneManagerImpl::renderToEvents(uint64_t startSample, uint32_t numSamples, bool loopEnabled, uint64_t loopStart, uint64_t loopEnd, Layer2::IEventQueue* eventQueue) const {
    // Deprecated: Real-time automation playback events are handled by the engine's AutomationProcessor.
    (void)startSample;
    (void)numSamples;
    (void)loopEnabled;
    (void)loopStart;
    (void)loopEnd;
    (void)eventQueue;
}

void AutomationLaneManagerImpl::applyDelta(const ProjectDelta& delta, bool isUndo) {
    if (delta.operationType == AutomationOps::CREATE_LANE) {
        AutomationLanePayload payload;
        std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(AutomationLanePayload));
        if (isUndo) {
            removeLane(payload.target, false);
        } else {
            createLane(payload.target, false);
        }
        return;
    }
    if (delta.operationType == AutomationOps::REMOVE_LANE) {
        AutomationLanePayload payload;
        std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(AutomationLanePayload));
        if (isUndo) {
            createLane(payload.target, false);
        } else {
            removeLane(payload.target, false);
        }
        return;
    }

    AutomationPointPayload payload;
    std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(AutomationPointPayload));
    
    auto* lane = createLane(payload.target);
    
    switch (delta.operationType) {
        case AutomationOps::ADD_POINT:
            if (isUndo) lane->removePoint(payload.samplePosition);
            else lane->addPoint(payload.samplePosition, payload.value, static_cast<::AutomationPoint::Shape>(payload.curveShape), payload.tension);
            break;
        case AutomationOps::REMOVE_POINT:
            if (isUndo) lane->addPoint(payload.samplePosition, payload.value, static_cast<::AutomationPoint::Shape>(payload.curveShape), payload.tension);
            else lane->removePoint(payload.samplePosition);
            break;
        case AutomationOps::UPDATE_POINT:
            lane->addPoint(payload.samplePosition, payload.value, static_cast<::AutomationPoint::Shape>(payload.curveShape), payload.tension);
            break;
        case AutomationOps::CLEAR_LANE:
            // Forward: erase all points. The compound wrapping clearAutomationLane()
            // already captured individual REMOVE_POINT deltas, so undo is handled by
            // those steps running in reverse — this case is a no-op on isUndo.
            if (!isUndo) lane->clearPoints();
            break;
        default:
            // Unrecognised operation — no-op. Do not silently fall through.
            break;
    }

}

void AutomationLaneManagerImpl::copyFrom(const AutomationLaneManagerImpl* other) {
    if (!other) return;
    mode_ = other->mode_;
    lanes_.clear();
    for (const auto& [target, lane] : other->lanes_) {
        auto newLane = std::make_unique<AutomationLaneImpl>(target, history_);
        if (auto* sourceImpl = dynamic_cast<const AutomationLaneImpl*>(lane.get())) {
            newLane->setPointsList(sourceImpl->getPointsList());
        }
        lanes_[target] = std::move(newLane);
    }
}

// =============================================================================
// Mode-guarded mutation entry points
// =============================================================================

bool AutomationLaneManagerImpl::addPoint(
    const AutomationTarget& target,
    uint64_t samplePosition,
    float value,
    ::AutomationPoint::Shape shape,
    float tension)
{
    if (mode_ == AutomationMode::OFF || mode_ == AutomationMode::READ) {
        return false; // Mode guard: write operations are not permitted.
    }
    // createLane is idempotent — returns existing lane if target already present.
    auto* lane = createLane(target);
    if (!lane) return false;
    lane->addPoint(samplePosition, value, shape, tension);
    return true;
}

bool AutomationLaneManagerImpl::removePoint(
    const AutomationTarget& target,
    uint64_t samplePosition)
{
    if (mode_ == AutomationMode::OFF || mode_ == AutomationMode::READ) {
        return false; // Mode guard: write operations are not permitted.
    }
    auto* lane = getLane(target);
    if (!lane) return false;
    lane->removePoint(samplePosition);
    return true;
}

void AutomationLaneManagerImpl::editPointShapeAndTension(TrackID trackId, NodeID nodeId, uint32_t subNodeId, uint32_t parameterIndex, uint32_t pointIndex, uint8_t shape, float tension) {
    (void)trackId;
    if (mode_ == AutomationMode::OFF || mode_ == AutomationMode::READ) {
        return; // Mode guard
    }
    for (auto& [target, lane] : lanes_) {
        if (target.nodeId == nodeId && target.subNodeId == subNodeId && target.cachedParameterIndex == parameterIndex) {
            auto* laneImpl = dynamic_cast<AutomationLaneImpl*>(lane.get());
            if (laneImpl) {
                laneImpl->editPointShapeAndTension(pointIndex, shape, tension);
            }
            break;
        }
    }
}

} // namespace composition
