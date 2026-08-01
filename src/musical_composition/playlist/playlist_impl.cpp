#include "playlist_impl.h"
#include "region_operations.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/region_manager/iaudio_region_source_manager.h"
#include <algorithm>
#include <cstring>
#include <atomic>

namespace composition {

static std::atomic<uint32_t> s_globalRegionIdCounter{0};

PlaylistImpl::PlaylistImpl(TrackID trackId, ICommandHistory* history, IAudioRegionSourceManager* sourceManager) 
    : trackId_(trackId), history_(history), sourceManager_(sourceManager) {}

RegionID PlaylistImpl::addRegion(const TimelineRegion& region, LayerIndex layer) {
    return addRegionInternal(region, layer, {0, 0}, true);
}

void PlaylistImpl::removeRegion(RegionID id) {
    removeRegionInternal(id, true);
}

RegionID PlaylistImpl::addRegionInternal(const TimelineRegion& region, LayerIndex layer, RegionID forcedId, bool pushDelta) {
    if (layer == AUTO_LAYER) {
        layer = 0;
        shiftOverlappingLayersDown(region.positionSample, region.sourceLength, 0);
    }

    if (forcedId.id != 0) {
        uint32_t current = s_globalRegionIdCounter.load();
        while (forcedId.id > current && !s_globalRegionIdCounter.compare_exchange_weak(current, forcedId.id)) {
            // current is automatically updated on failure
        }
    }

    RegionID id = (forcedId.id != 0) ? forcedId : generateNextId();
    RegionEntry entry{ id, region, layer };
    regions_.push_back(entry);

    if (region.type == RegionType::AUDIO && sourceManager_) {
        sourceManager_->incrementReference(region.sourceId);
    }

    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::PLAYLIST;
        delta.operationType = PlaylistOps::ADD_REGION;
        delta.targetId = handleToUint64(trackId_); 
        
        AddRegionPayload payload{ id, region, layer };
        delta.newStateSize = sizeof(AddRegionPayload);
        std::memcpy(delta.newState, &payload, sizeof(AddRegionPayload));
        
        history_->pushDelta(delta);
    }

    return id;
}

void PlaylistImpl::removeRegionInternal(RegionID id, bool pushDelta) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
        [&](const RegionEntry& e) { return e.regionId == id; });
    
    if (it != regions_.end()) {
        if (it->region.type == RegionType::AUDIO && sourceManager_) {
            sourceManager_->decrementReference(it->region.sourceId);
        }

        if (pushDelta && history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::PLAYLIST;
            delta.operationType = PlaylistOps::REMOVE_REGION;
            delta.targetId = handleToUint64(trackId_);
            
            AddRegionPayload payload{ it->regionId, it->region, it->layer };
            delta.oldStateSize = sizeof(AddRegionPayload);
            std::memcpy(delta.oldState, &payload, sizeof(AddRegionPayload));
            
            history_->pushDelta(delta);
        }
        regions_.erase(it);
    }
}

void PlaylistImpl::moveRegion(RegionID id, uint64_t newPosition, LayerIndex newLayer) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
        [&](const RegionEntry& e) { return e.regionId == id; });
    
    if (it != regions_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::PLAYLIST;
            delta.operationType = PlaylistOps::MOVE_REGION;
            delta.targetId = handleToUint64(id); // Target the region handle
            
            MoveRegionPayload oldP{ it->region.positionSample, it->layer };
            delta.oldStateSize = sizeof(MoveRegionPayload);
            std::memcpy(delta.oldState, &oldP, sizeof(MoveRegionPayload));
            
            MoveRegionPayload newP{ newPosition, newLayer };
            delta.newStateSize = sizeof(MoveRegionPayload);
            std::memcpy(delta.newState, &newP, sizeof(MoveRegionPayload));
            
            history_->pushDelta(delta);
        }
        
        it->region.positionSample = newPosition;
        it->layer = newLayer;
    }
}

void PlaylistImpl::trimRegion(RegionID id, uint64_t newPosition, uint64_t newSourceStart, uint64_t newSourceLength) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
        [&](const RegionEntry& e) { return e.regionId == id; });
    
    if (it != regions_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::PLAYLIST;
            delta.operationType = PlaylistOps::TRIM_REGION;
            delta.targetId = handleToUint64(id);
            
            TrimRegionPayload oldP{ it->region.positionSample, it->region.sourceStartSample, it->region.sourceLength };
            delta.oldStateSize = sizeof(TrimRegionPayload);
            std::memcpy(delta.oldState, &oldP, sizeof(TrimRegionPayload));
            
            TrimRegionPayload newP{ newPosition, newSourceStart, newSourceLength };
            delta.newStateSize = sizeof(TrimRegionPayload);
            std::memcpy(delta.newState, &newP, sizeof(TrimRegionPayload));
            
            history_->pushDelta(delta);
        }
        
        it->region.positionSample = newPosition;
        it->region.sourceStartSample = newSourceStart;
        it->region.sourceLength = newSourceLength;
    }
}

void PlaylistImpl::setProjectSampleRate(uint32_t sampleRate) {
    projectSampleRate_ = sampleRate;
}

RegionID PlaylistImpl::splitRegion(RegionID id, uint64_t splitPointSample, uint64_t sourceOffsetSample) {
    return splitRegionInternal(id, splitPointSample, sourceOffsetSample, {0, 0}, true);
}

RegionID PlaylistImpl::splitRegionInternal(RegionID id, uint64_t splitPointSample, uint64_t sourceOffsetSample, RegionID forcedRightId, bool pushDelta) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
        [&](const RegionEntry& e) { return e.regionId == id; });
    
    if (it == regions_.end()) return {0, 0};

    // Calculate sourceOffset if 0
    uint64_t sourceOffset = sourceOffsetSample;
    if (sourceOffset == 0) {
        if (it->region.type == RegionType::AUDIO) {
            uint64_t projectOffset = splitPointSample - it->region.positionSample;
            double srRatio = 1.0;
            if (sourceManager_) {
                composition::AudioSourceDescriptor desc;
                if (sourceManager_->getSource(it->region.sourceId, desc) && desc.sampleRate > 0) {
                    srRatio = static_cast<double>(desc.sampleRate) / static_cast<double>(projectSampleRate_);
                }
            }
            double ratio = (it->region.playbackRatio > 0.0f ? static_cast<double>(it->region.playbackRatio) : 1.0) * srRatio;
            sourceOffset = static_cast<uint64_t>(static_cast<double>(projectOffset) * ratio);
        } else {
            sourceOffset = splitPointSample - it->region.positionSample;
        }
    }

    // Verify split point is within the region
    if (splitPointSample <= it->region.positionSample || 
        sourceOffset >= it->region.sourceLength) {
        return {0, 0};
    }

    LayerIndex cachedLayer = it->layer; // Bug Fix: Cache layer before push_back invalidates iterator
    
    // Create right region
    TimelineRegion rightRegion = it->region;
    rightRegion.positionSample = splitPointSample;
    rightRegion.sourceStartSample += sourceOffset;
    rightRegion.sourceLength -= sourceOffset;
    
    // For MIDI regions, assign a unique ClipID to the right region's sourceId
    if (rightRegion.type == RegionType::MIDI) {
        ClipID newClipId{ ++getGlobalClipIdCounter(), 1 };
        rightRegion.sourceId = SourceID::fromRaw(newClipId.toRaw());
    } else if (rightRegion.type == RegionType::AUDIO && sourceManager_) {
        sourceManager_->incrementReference(rightRegion.sourceId);
    }
    
    // Adjust left region
    it->region.sourceLength = sourceOffset;
    
    if (forcedRightId.id != 0) {
        uint32_t current = s_globalRegionIdCounter.load();
        while (forcedRightId.id > current && !s_globalRegionIdCounter.compare_exchange_weak(current, forcedRightId.id)) {
            // current is automatically updated on failure
        }
    }
    
    RegionID rightId = (forcedRightId.id != 0) ? forcedRightId : generateNextId();
    regions_.push_back({ rightId, rightRegion, cachedLayer });
    
    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::PLAYLIST;
        delta.operationType = PlaylistOps::SPLIT_REGION;
        delta.targetId = handleToUint64(id);
        
        SplitRegionPayload payload{ splitPointSample, sourceOffset, id, rightId };
        delta.newStateSize = sizeof(SplitRegionPayload);
        std::memcpy(delta.newState, &payload, sizeof(SplitRegionPayload));
        
        history_->pushDelta(delta);
    }
    
    return rightId;
}

void PlaylistImpl::setFades(RegionID id, uint32_t fadeInSamples, uint32_t fadeOutSamples) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
        [&](const RegionEntry& e) { return e.regionId == id; });
    
    if (it != regions_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::PLAYLIST;
            delta.operationType = PlaylistOps::FADES_REGION;
            delta.targetId = handleToUint64(id);
            
            FadesRegionPayload oldP{ it->region.fadeInSamples, it->region.fadeOutSamples };
            delta.oldStateSize = sizeof(FadesRegionPayload);
            std::memcpy(delta.oldState, &oldP, sizeof(FadesRegionPayload));
            
            FadesRegionPayload newP{ fadeInSamples, fadeOutSamples };
            delta.newStateSize = sizeof(FadesRegionPayload);
            std::memcpy(delta.newState, &newP, sizeof(FadesRegionPayload));
            
            history_->pushDelta(delta);
        }
        
        it->region.fadeInSamples = fadeInSamples;
        it->region.fadeOutSamples = fadeOutSamples;
    }
}

uint32_t PlaylistImpl::getAllRegions(RegionInfo* outRegions, uint32_t maxRegions) const {
    uint32_t count = 0;
    for (const auto& entry : regions_) {
        if (count < maxRegions) {
            outRegions[count].id = entry.regionId;
            outRegions[count].region = entry.region;
            outRegions[count].layer = entry.layer;
            count++;
        } else {
            break;
        }
    }
    return count;
}


uint32_t PlaylistImpl::getRegionsAt(uint64_t samplePos, TimelineRegion* outRegions, uint32_t maxRegions) const {
    uint32_t count = 0;
    for (const auto& entry : regions_) {
        if (samplePos >= entry.region.positionSample && 
            samplePos < (entry.region.positionSample + entry.region.sourceLength)) {
            if (count < maxRegions) {
                outRegions[count] = entry.region;
                count++;
            }
        }
    }
    return count;
}

uint32_t PlaylistImpl::getMaxLayer() const {
    uint32_t maxLayer = 0;
    bool hasRegions = false;
    for (const auto& entry : regions_) {
        if (entry.layer > maxLayer) {
            maxLayer = entry.layer;
        }
        hasRegions = true;
    }
    return hasRegions ? (maxLayer + 1) : 1;
}

RegionID PlaylistImpl::generateNextId() {
    return { ++s_globalRegionIdCounter, 1 };
}

PlaylistImpl::LayerIndex PlaylistImpl::findFreeLayer(uint64_t start, uint64_t length) const {
    for (LayerIndex l = 0; l < MAX_LAYERS; ++l) {
        if (isLayerFree(l, start, length)) return l;
    }
    return 0; // Fallback to layer 0
}

bool PlaylistImpl::isLayerFree(LayerIndex layer, uint64_t start, uint64_t length) const {
    uint64_t end = start + length;
    for (const auto& entry : regions_) {
        if (entry.layer == layer) {
            uint64_t rStart = entry.region.positionSample;
            uint64_t rEnd = rStart + entry.region.sourceLength;
            if (start < rEnd && end > rStart) return false; // Overlap
        }
    }
    return true;
}

void PlaylistImpl::shiftOverlappingLayersDown(uint64_t start, uint64_t length, LayerIndex startingLayer) {
    uint64_t end = start + length;
    std::vector<RegionID> toShift;
    
    for (const auto& entry : regions_) {
        if (entry.layer == startingLayer) {
            uint64_t rStart = entry.region.positionSample;
            uint64_t rEnd = rStart + entry.region.sourceLength;
            if (start < rEnd && end > rStart) {
                toShift.push_back(entry.regionId);
            }
        }
    }
    
    for (auto id : toShift) {
        auto it = std::find_if(regions_.begin(), regions_.end(),
            [&](const RegionEntry& e) { return e.regionId == id; });
        if (it != regions_.end()) {
            LayerIndex newLayer = it->layer + 1;
            uint64_t rStart = it->region.positionSample;
            uint64_t rLength = it->region.sourceLength;
            
            shiftOverlappingLayersDown(rStart, rLength, newLayer);
            moveRegion(id, rStart, newLayer);
        }
    }
}

void PlaylistImpl::applyDelta(const ProjectDelta& delta, bool isUndo) {
    switch (delta.operationType) {
        case PlaylistOps::ADD_REGION: {
            AddRegionPayload payload;
            // Bug Fix: Always read from newState for ADD_REGION as oldState is empty
            std::memcpy(&payload, delta.newState, sizeof(AddRegionPayload));
            if (isUndo) {
                removeRegionInternal(payload.regionId, false);
            } else {
                addRegionInternal(payload.region, payload.layer, payload.regionId, false); 
            }
            break;
        }
        case PlaylistOps::REMOVE_REGION: {
            AddRegionPayload payload;
            // Bug Fix: Always read from oldState for REMOVE_REGION as newState is empty
            std::memcpy(&payload, delta.oldState, sizeof(AddRegionPayload));
            if (isUndo) {
                addRegionInternal(payload.region, payload.layer, payload.regionId, false);
            } else {
                removeRegionInternal(payload.regionId, false);
            }
            break;
        }
        case PlaylistOps::MOVE_REGION: {
            MoveRegionPayload payload;
            std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(MoveRegionPayload));
            moveRegion(uint64ToHandle<RegionID>(delta.targetId), payload.newPositionSample, payload.newLayer);
            break;
        }
        case PlaylistOps::TRIM_REGION: {
            TrimRegionPayload payload;
            std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(TrimRegionPayload));
            trimRegion(uint64ToHandle<RegionID>(delta.targetId), payload.newPositionSample, payload.newSourceStartSample, payload.newSourceLength);
            break;
        }
        case PlaylistOps::SPLIT_REGION: {
            SplitRegionPayload payload;
            std::memcpy(&payload, delta.newState, sizeof(SplitRegionPayload));
            if (isUndo) {
                auto leftIt = std::find_if(regions_.begin(), regions_.end(),
                    [&](const RegionEntry& e) { return e.regionId == uint64ToHandle<RegionID>(delta.targetId); });
                auto rightIt = std::find_if(regions_.begin(), regions_.end(),
                    [&](const RegionEntry& e) { return e.regionId == payload.rightRegionId; });
                
                if (leftIt != regions_.end() && rightIt != regions_.end()) {
                    leftIt->region.sourceLength += rightIt->region.sourceLength;
                }
                removeRegionInternal(payload.rightRegionId, false);
            } else {
                splitRegionInternal(uint64ToHandle<RegionID>(delta.targetId), payload.splitPointSample, payload.sourceOffsetSample, payload.rightRegionId, false);
            }
            break;
        }
        case PlaylistOps::FADES_REGION: {
            FadesRegionPayload payload;
            std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(FadesRegionPayload));
            setFades(uint64ToHandle<RegionID>(delta.targetId), payload.fadeInSamples, payload.fadeOutSamples);
            break;
        }
        case PlaylistOps::WARP_REGION: {
            WarpRegionPayload payload;
            std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(WarpRegionPayload));
            setWarpMode(uint64ToHandle<RegionID>(delta.targetId), payload.warpMode);
            setPlaybackRatio(uint64ToHandle<RegionID>(delta.targetId), payload.playbackRatio);
            setSourceBpm(uint64ToHandle<RegionID>(delta.targetId), payload.sourceBpm);
            break;
        }
        case PlaylistOps::MUTE_REGION: {
            MuteRegionPayload payload;
            std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(MuteRegionPayload));
            setRegionMuted(uint64ToHandle<RegionID>(delta.targetId), isUndo ? payload.oldMuted : payload.newMuted);
            break;
        }
        case PlaylistOps::GAIN_REGION: {
            GainRegionPayload payload;
            std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(GainRegionPayload));
            setRegionGain(uint64ToHandle<RegionID>(delta.targetId), isUndo ? payload.oldGain : payload.newGain);
            break;
        }
    }
}

void PlaylistImpl::setWarpMode(RegionID id, WarpMode mode) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
        [&](const RegionEntry& e) { return e.regionId == id; });
    
    if (it != regions_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::PLAYLIST;
            delta.operationType = PlaylistOps::WARP_REGION;
            delta.targetId = handleToUint64(id);
            
            WarpRegionPayload oldP{ it->region.warpMode, it->region.playbackRatio, it->region.sourceBpm };
            delta.oldStateSize = sizeof(WarpRegionPayload);
            std::memcpy(delta.oldState, &oldP, sizeof(WarpRegionPayload));
            
            WarpRegionPayload newP{ mode, it->region.playbackRatio, it->region.sourceBpm };
            delta.newStateSize = sizeof(WarpRegionPayload);
            std::memcpy(delta.newState, &newP, sizeof(WarpRegionPayload));
            
            history_->pushDelta(delta);
        }
        it->region.warpMode = mode;
    }
}

void PlaylistImpl::setPlaybackRatio(RegionID id, float ratio) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
        [&](const RegionEntry& e) { return e.regionId == id; });
    
    if (it != regions_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::PLAYLIST;
            delta.operationType = PlaylistOps::WARP_REGION;
            delta.targetId = handleToUint64(id);
            
            WarpRegionPayload oldP{ it->region.warpMode, it->region.playbackRatio, it->region.sourceBpm };
            delta.oldStateSize = sizeof(WarpRegionPayload);
            std::memcpy(delta.oldState, &oldP, sizeof(WarpRegionPayload));
            
            WarpRegionPayload newP{ it->region.warpMode, ratio, it->region.sourceBpm };
            delta.newStateSize = sizeof(WarpRegionPayload);
            std::memcpy(delta.newState, &newP, sizeof(WarpRegionPayload));
            
            history_->pushDelta(delta);
        }
        it->region.playbackRatio = ratio;
    }
}

void PlaylistImpl::setSourceBpm(RegionID id, float bpm) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
        [&](const RegionEntry& e) { return e.regionId == id; });
    
    if (it != regions_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::PLAYLIST;
            delta.operationType = PlaylistOps::WARP_REGION;
            delta.targetId = handleToUint64(id);
            
            WarpRegionPayload oldP{ it->region.warpMode, it->region.playbackRatio, it->region.sourceBpm };
            delta.oldStateSize = sizeof(WarpRegionPayload);
            std::memcpy(delta.oldState, &oldP, sizeof(WarpRegionPayload));
            
            WarpRegionPayload newP{ it->region.warpMode, it->region.playbackRatio, bpm };
            delta.newStateSize = sizeof(WarpRegionPayload);
            std::memcpy(delta.newState, &newP, sizeof(WarpRegionPayload));
            
            history_->pushDelta(delta);
        }
        it->region.sourceBpm = bpm;
    }
}

void PlaylistImpl::setRegionMuted(RegionID id, bool muted) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
        [&](const RegionEntry& e) { return e.regionId == id; });
    
    if (it != regions_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::PLAYLIST;
            delta.operationType = PlaylistOps::MUTE_REGION;
            delta.targetId = handleToUint64(id);
            
            MuteRegionPayload oldP{ it->region.isMuted, muted };
            delta.oldStateSize = sizeof(MuteRegionPayload);
            std::memcpy(delta.oldState, &oldP, sizeof(MuteRegionPayload));
            
            MuteRegionPayload newP{ it->region.isMuted, muted };
            delta.newStateSize = sizeof(MuteRegionPayload);
            std::memcpy(delta.newState, &newP, sizeof(MuteRegionPayload));
            
            history_->pushDelta(delta);
        }
        it->region.isMuted = muted;
    }
}

void PlaylistImpl::setRegionGain(RegionID id, float gain) {
    auto it = std::find_if(regions_.begin(), regions_.end(),
        [&](const RegionEntry& e) { return e.regionId == id; });
    
    if (it != regions_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::PLAYLIST;
            delta.operationType = PlaylistOps::GAIN_REGION;
            delta.targetId = handleToUint64(id);
            
            GainRegionPayload oldP{ it->region.gain, gain };
            delta.oldStateSize = sizeof(GainRegionPayload);
            std::memcpy(delta.oldState, &oldP, sizeof(GainRegionPayload));
            
            GainRegionPayload newP{ it->region.gain, gain };
            delta.newStateSize = sizeof(GainRegionPayload);
            std::memcpy(delta.newState, &newP, sizeof(GainRegionPayload));
            
            history_->pushDelta(delta);
        }
        it->region.gain = gain;
    }
}

void PlaylistImpl::copyFrom(const PlaylistImpl* other) {
    if (!other) return;
    regions_ = other->regions_;
}

void PlaylistImpl::restoreRegion(RegionID id, const TimelineRegion& region, LayerIndex layer) {
    addRegionInternal(region, layer, id, false);
}

} // namespace composition
