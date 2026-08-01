#include "automation_lane_impl.h"
#include "automation_commands.h"
#include "musical_composition/command_history/icommand_history.h"
#include <algorithm>
#include <cstring>

namespace composition {

AutomationLaneImpl::AutomationLaneImpl(const AutomationTarget& target, ICommandHistory* history)
    : target_(target), history_(history) {}

void AutomationLaneImpl::addPoint(uint64_t samplePosition, float value) {
    addPoint(samplePosition, value, ::AutomationPoint::Shape::LINEAR, 0.5f);
}

void AutomationLaneImpl::addPoint(uint64_t samplePosition, float value, ::AutomationPoint::Shape shape, float tension) {
    Point p{ samplePosition, value, shape, tension };
    
    auto it = std::lower_bound(points_.begin(), points_.end(), p, 
        [](const Point& a, const Point& b) { return a.positionSample < b.positionSample; });
    
    bool isUpdate = false;
    Point oldPoint{};

    // If point exists at this position, update it
    if (it != points_.end() && it->positionSample == samplePosition) {
        oldPoint = *it;
        it->value = value;
        it->curveShape = shape;
        it->tension = tension;
        isUpdate = true;
    } else {
        points_.insert(it, p);
    }
    
    if (history_) {
        static_assert(std::is_trivially_copyable_v<AutomationPointPayload>, "AutomationPointPayload must be trivially copyable");
        
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::AUTOMATION;
        delta.operationType = isUpdate ? AutomationOps::UPDATE_POINT : AutomationOps::ADD_POINT;
        delta.targetId = target_.nodeId.toRaw();
        AutomationPointPayload payload{ target_, samplePosition, value, static_cast<uint8_t>(shape), tension };
        delta.newStateSize = sizeof(AutomationPointPayload);
        std::memcpy(delta.newState, &payload, sizeof(AutomationPointPayload));
        
        if (isUpdate) {
            AutomationPointPayload oldPayload{ target_, oldPoint.positionSample, oldPoint.value, static_cast<uint8_t>(oldPoint.curveShape), oldPoint.tension };
            delta.oldStateSize = sizeof(AutomationPointPayload);
            std::memcpy(delta.oldState, &oldPayload, sizeof(AutomationPointPayload));
        }
        
        history_->pushDelta(delta);
    }
}

void AutomationLaneImpl::removePoint(uint64_t samplePosition) {
    auto it = std::find_if(points_.begin(), points_.end(),
        [pos = samplePosition](const Point& p) { return p.positionSample == pos; });
    
    if (it != points_.end()) {
        if (history_) {
            static_assert(std::is_trivially_copyable_v<AutomationPointPayload>, "AutomationPointPayload must be trivially copyable");
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::AUTOMATION;
            delta.operationType = AutomationOps::REMOVE_POINT;
            delta.targetId = target_.nodeId.toRaw();
            
            AutomationPointPayload oldPayload{ target_, it->positionSample, it->value, static_cast<uint8_t>(it->curveShape), it->tension };
            delta.oldStateSize = sizeof(AutomationPointPayload);
            std::memcpy(delta.oldState, &oldPayload, sizeof(AutomationPointPayload));
            
            history_->pushDelta(delta);
        }
        points_.erase(it);
    }
}

void AutomationLaneImpl::editPointShapeAndTension(uint32_t index, uint8_t shape, float tension) {
    if (index >= points_.size()) return;

    Point oldPoint = points_[index];
    points_[index].curveShape = static_cast<::AutomationPoint::Shape>(shape);
    points_[index].tension = tension;

    if (history_) {
        static_assert(std::is_trivially_copyable_v<AutomationPointPayload>, "AutomationPointPayload must be trivially copyable");
        
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::AUTOMATION;
        delta.operationType = AutomationOps::UPDATE_POINT;
        delta.targetId = target_.nodeId.toRaw();
        AutomationPointPayload payload{ target_, points_[index].positionSample, points_[index].value, shape, tension };
        delta.newStateSize = sizeof(AutomationPointPayload);
        std::memcpy(delta.newState, &payload, sizeof(AutomationPointPayload));
        
        AutomationPointPayload oldPayload{ target_, oldPoint.positionSample, oldPoint.value, static_cast<uint8_t>(oldPoint.curveShape), oldPoint.tension };
        delta.oldStateSize = sizeof(AutomationPointPayload);
        std::memcpy(delta.oldState, &oldPayload, sizeof(AutomationPointPayload));
        
        history_->pushDelta(delta);
    }
}

void AutomationLaneImpl::clearPoints() {
    // Intentionally does NOT push any delta. Callers (e.g. clearAutomationLane on
    // the bridge) must emit the individual REMOVE_POINT compound steps BEFORE
    // calling this so that undo can restore each point individually.
    points_.clear();
}


float AutomationLaneImpl::evaluate(double frame) const {
    if (points_.empty()) return 0.0f;
    uint64_t samplePosition = static_cast<uint64_t>(std::round(frame));
    if (samplePosition <= points_.front().positionSample) return points_.front().value;
    if (samplePosition >= points_.back().positionSample) return points_.back().value;

    // Find the pair of points surrounding samplePosition
    auto it = std::lower_bound(points_.begin(), points_.end(), samplePosition,
        [](const Point& p, uint64_t pos) { return p.positionSample < pos; });
    
    const Point& p2 = *it;
    const Point& p1 = *std::prev(it);
    
    return DSP::CurveInterpolator::calculate(p1, p2, samplePosition);
}

uint32_t AutomationLaneImpl::getPoints(Point* outPoints, uint32_t maxPoints) const {
    if (!outPoints || maxPoints == 0) {
        return static_cast<uint32_t>(points_.size());
    }
    uint32_t count = 0;
    for (const auto& p : points_) {
        if (count < maxPoints) {
            outPoints[count] = p;
            count++;
        } else {
            break;
        }
    }
    return count;
}

} // namespace composition
