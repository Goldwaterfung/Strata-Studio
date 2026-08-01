#include "comping_engine_impl.h"
#include "comping_commands.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/playlist/iplaylist.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace composition {

CompingEngineImpl::CompingEngineImpl(ICommandHistory* history) : history_(history) {}

LaneID CompingEngineImpl::createLane(uint64_t positionSample, uint64_t lengthSamples) {
    LaneID newId = generateNextLaneId();
    TakeLane lane{ newId, 0, positionSample, lengthSamples, {} };
    lanes_[newId.id] = lane;

    if (history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::COMPING;
        delta.operationType = CompingOps::CREATE_LANE;
        delta.targetId = handleToUint64(newId);
        
        CreateLanePayload payload{ newId.id, positionSample, lengthSamples };
        delta.newStateSize = sizeof(CreateLanePayload);
        std::memcpy(delta.newState, &payload, sizeof(CreateLanePayload));
        
        history_->pushDelta(delta);
    }

    return newId;
}

TakeID CompingEngineImpl::createTake(LaneID laneId, RegionID underlyingRegion) {
    auto it = lanes_.find(laneId.id);
    if (it == lanes_.end()) return {0, 0};

    TakeID newId = generateNextTakeId();
    Take take{ newId, underlyingRegion, it->second.laneIndex, false };
    takes_[newId.id] = take;
    it->second.takes.push_back(newId);

    if (history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::COMPING;
        delta.operationType = CompingOps::CREATE_TAKE;
        delta.targetId = handleToUint64(newId);
        
        TakePayload payload{ laneId.id, underlyingRegion.id, false };
        delta.newStateSize = sizeof(TakePayload);
        std::memcpy(delta.newState, &payload, sizeof(TakePayload));
        
        history_->pushDelta(delta);
    }

    return newId;
}

bool CompingEngineImpl::selectTake(TakeID takeId) {
    auto itTake = takes_.find(takeId.id);
    if (itTake == takes_.end()) return false;

    // Find the lane this take belongs to and currently selected take
    uint32_t targetLaneId = 0;
    TakeID prevSelectedId = {0, 0};

    for (auto& [laneId, lane] : lanes_) {
        auto it = std::find_if(lane.takes.begin(), lane.takes.end(), 
            [takeId](TakeID id) { return id == takeId; });
        if (it != lane.takes.end()) {
            targetLaneId = laneId;
            for (TakeID tid : lane.takes) {
                if (takes_[tid.id].isSelected) {
                    prevSelectedId = tid;
                    break;
                }
            }
            break;
        }
    }

    if (targetLaneId == 0) return false;

    // Deselect all takes in this lane, then select the target
    auto& lane = lanes_[targetLaneId];
    for (TakeID tid : lane.takes) {
        takes_[tid.id].isSelected = (tid == takeId);
    }

    if (history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::COMPING;
        delta.operationType = CompingOps::SELECT_TAKE;
        delta.targetId = handleToUint64(takeId);
        
        SelectTakePayload newP{ takeId.id, prevSelectedId.id };
        delta.newStateSize = sizeof(SelectTakePayload);
        std::memcpy(delta.newState, &newP, sizeof(SelectTakePayload));

        SelectTakePayload oldP{ prevSelectedId.id, takeId.id };
        delta.oldStateSize = sizeof(SelectTakePayload);
        std::memcpy(delta.oldState, &oldP, sizeof(SelectTakePayload));

        history_->pushDelta(delta);
    }

    return true;
}

void CompingEngineImpl::swapTakeLayer(IPlaylist* targetPlaylist, LayerIndex sourceLayer, uint64_t startSample, uint64_t length) {
    if (!targetPlaylist || sourceLayer == 0) return;

    std::vector<IPlaylist::RegionInfo> regions;
    
    // Helper to safely fetch all regions
    auto fetchAllRegions = [&]() {
        uint32_t count = targetPlaylist->getAllRegions(nullptr, 0);
        uint32_t fetched = 0;
        do {
            regions.resize(count + 16); // small buffer
            fetched = targetPlaylist->getAllRegions(regions.data(), static_cast<uint32_t>(regions.size()));
            count = fetched;
        } while (fetched == regions.size());
        regions.resize(fetched);
    };
    
    fetchAllRegions();

    if (length > 0) {
        uint64_t endSample = startSample + length;
        std::vector<RegionID> toSplitStart;
        for (const auto& entry : regions) {
            if (entry.layer == 0 || entry.layer == sourceLayer) {
                if (entry.region.positionSample < startSample && entry.region.positionSample + entry.region.sourceLength > startSample) {
                    toSplitStart.push_back(entry.id);
                }
            }
        }
        for (auto id : toSplitStart) targetPlaylist->splitRegion(id, startSample);

        fetchAllRegions();

        std::vector<RegionID> toSplitEnd;
        for (const auto& entry : regions) {
            if (entry.layer == 0 || entry.layer == sourceLayer) {
                if (entry.region.positionSample < endSample && entry.region.positionSample + entry.region.sourceLength > endSample) {
                    toSplitEnd.push_back(entry.id);
                }
            }
        }
        for (auto id : toSplitEnd) targetPlaylist->splitRegion(id, endSample);

        fetchAllRegions();

        std::vector<std::pair<RegionID, LayerIndex>> toSwap;
        for (const auto& entry : regions) {
            if (entry.region.positionSample >= startSample && (entry.region.positionSample + entry.region.sourceLength) <= endSample) {
                if (entry.layer == sourceLayer) {
                    toSwap.push_back({entry.id, 0});
                } else if (entry.layer == 0) {
                    toSwap.push_back({entry.id, sourceLayer});
                }
            }
        }
        for (auto pair : toSwap) {
            auto it = std::find_if(regions.begin(), regions.end(), [pair](const IPlaylist::RegionInfo& e) { return e.id.id == pair.first.id; });
            if (it != regions.end()) {
                targetPlaylist->moveRegion(pair.first, it->region.positionSample, pair.second);
            }
        }
    } else {
        std::vector<std::pair<RegionID, LayerIndex>> toSwap;
        for (const auto& entry : regions) {
            if (entry.layer == sourceLayer) {
                toSwap.push_back({entry.id, 0});
            } else if (entry.layer == 0) {
                toSwap.push_back({entry.id, sourceLayer});
            }
        }
        for (auto pair : toSwap) {
            auto it = std::find_if(regions.begin(), regions.end(), [pair](const IPlaylist::RegionInfo& e) { return e.id.id == pair.first.id; });
            if (it != regions.end()) {
                targetPlaylist->moveRegion(pair.first, it->region.positionSample, pair.second);
            }
        }
    }
}

LaneID CompingEngineImpl::generateNextLaneId() {
    return { ++nextLaneIdCounter_, 1 };
}

TakeID CompingEngineImpl::generateNextTakeId() {
    return { ++nextTakeIdCounter_, 1 };
}

std::unique_ptr<ICompingEngine> ICompingEngine::create(ICommandHistory* history) {
    return std::make_unique<CompingEngineImpl>(history);
}

void CompingEngineImpl::applyDelta(const ProjectDelta& delta, bool isUndo) {
    switch (delta.operationType) {
        case CompingOps::CREATE_LANE: {
            CreateLanePayload payload;
            // Bug Fix: Always read from newState for CREATE_LANE as oldState is empty
            std::memcpy(&payload, delta.newState, sizeof(CreateLanePayload));
            if (isUndo) {
                lanes_.erase(payload.laneId);
            } else {
                createLane(payload.positionSample, payload.lengthSamples);
            }
            break;
        }
        case CompingOps::SELECT_TAKE: {
            SelectTakePayload payload;
            std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(SelectTakePayload));
            if (payload.takeId != 0) {
                selectTake(uint64ToHandle<TakeID>(payload.takeId));
            } else {
                // If takeId is 0, it means deselect all in that lane (if supported)
            }
            break;
        }
    }
}

} // namespace composition
