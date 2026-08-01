#pragma once
#include "icomping_engine.h"
#include "musical_composition/command_history/delta_primitives.h"
#include <unordered_map>
#include <vector>

namespace composition {

class ICommandHistory;

class CompingEngineImpl : public ICompingEngine {
public:
    CompingEngineImpl(ICommandHistory* history);

    LaneID createLane(uint64_t positionSample, uint64_t lengthSamples) override;
    TakeID createTake(LaneID laneId, RegionID underlyingRegion) override;
    
    bool selectTake(TakeID takeId) override;
    
    void swapTakeLayer(IPlaylist* targetPlaylist, LayerIndex sourceLayer, uint64_t startSample, uint64_t length) override;
    
    void applyDelta(const ProjectDelta& delta, bool isUndo);

private:
    ICommandHistory* history_;
    
    std::unordered_map<uint32_t, TakeLane> lanes_;
    std::unordered_map<uint32_t, Take> takes_;
    
    uint32_t nextLaneIdCounter_ = 0;
    uint32_t nextTakeIdCounter_ = 0;

    LaneID generateNextLaneId();
    TakeID generateNextTakeId();
};

} // namespace composition
