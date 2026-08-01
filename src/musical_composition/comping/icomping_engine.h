#pragma once
#include "take_lane.h"
#include <memory>

namespace composition {

class ICompingEngine {
public:
    virtual ~ICompingEngine() = default;

    static std::unique_ptr<ICompingEngine> create(class ICommandHistory* history = nullptr);

    virtual LaneID createLane(uint64_t positionSample, uint64_t lengthSamples) = 0;
    virtual TakeID createTake(LaneID laneId, RegionID underlyingRegion) = 0;
    
    virtual bool selectTake(TakeID takeId) = 0;
    
    using LayerIndex = uint32_t;
    virtual void swapTakeLayer(class IPlaylist* targetPlaylist, LayerIndex sourceLayer, uint64_t startSample, uint64_t length) = 0;
};

} // namespace composition
