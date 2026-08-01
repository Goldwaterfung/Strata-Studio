#pragma once
#include "iautomation_lane.h"
#include "common/dsp/curve_interpolation.h"
#include <vector>

namespace composition {

class ICommandHistory;

class AutomationLaneImpl : public IAutomationLane {
public:
    AutomationLaneImpl(const AutomationTarget& target, ICommandHistory* history);

    void addPoint(uint64_t samplePosition, float value) override;
    void addPoint(uint64_t samplePosition, float value, ::AutomationPoint::Shape shape, float tension) override;
    void removePoint(uint64_t samplePosition) override;
    void editPointShapeAndTension(uint32_t index, uint8_t shape, float tension);
    void clearPoints() override;
    float evaluate(double frame) const override;
    uint32_t getPoints(Point* outPoints, uint32_t maxPoints) const override;

    const AutomationTarget& getTarget() const override { return target_; }
    const std::vector<Point>& getPointsList() const { return points_; }
    void setPointsList(const std::vector<Point>& points) { points_ = points; }

private:
    AutomationTarget target_;
    ICommandHistory* history_;
    std::vector<Point> points_;
};

} // namespace composition
