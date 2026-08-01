#include "region_metadata_manager_impl.h"
#include "musical_composition/command_history/region_metadata_commands.h"
#include <cstring>
#include <iostream>

namespace composition {

RegionMetadataManagerImpl::RegionMetadataManagerImpl(ICommandHistory* history)
    : history_(history) {}

void RegionMetadataManagerImpl::setRegionMetadata(RegionID id, const RegionMetadata& metadata, bool pushDelta) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    RegionMetadata oldMetadata{};
    bool hadOld = false;
    auto it = metadataMap_.find(id);
    if (it != metadataMap_.end()) {
        oldMetadata = it->second;
        hadOld = true;
    }
    
    metadataMap_[id] = metadata;
    
    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::REGION_METADATA;
        delta.operationType = RegionMetadataOps::SET_METADATA;
        delta.targetId = id.toRaw();
        
        RegionMetadataPayload newPayload{};
        newPayload.regionId = id;
        std::memcpy(newPayload.name, metadata.name, MAX_NAME_LENGTH);
        std::memcpy(newPayload.comment, metadata.comment, sizeof(newPayload.comment));
        newPayload.colorARGB = metadata.colorARGB;
        newPayload.hasComment = metadata.hasComment;
        
        delta.newStateSize = sizeof(RegionMetadataPayload);
        std::memcpy(delta.newState, &newPayload, sizeof(RegionMetadataPayload));
        
        if (hadOld) {
            RegionMetadataPayload oldPayload{};
            oldPayload.regionId = id;
            std::memcpy(oldPayload.name, oldMetadata.name, MAX_NAME_LENGTH);
            std::memcpy(oldPayload.comment, oldMetadata.comment, sizeof(oldPayload.comment));
            oldPayload.colorARGB = oldMetadata.colorARGB;
            oldPayload.hasComment = oldMetadata.hasComment;
            
            delta.oldStateSize = sizeof(RegionMetadataPayload);
            std::memcpy(delta.oldState, &oldPayload, sizeof(RegionMetadataPayload));
        } else {
            delta.oldStateSize = 0;
        }
        
        history_->pushDelta(delta);
    }
}

void RegionMetadataManagerImpl::getRegionMetadata(RegionID id, RegionMetadata& outMetadata) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = metadataMap_.find(id);
    if (it != metadataMap_.end()) {
        outMetadata = it->second;
    } else {
        // Enforce the guaranteed-existence invariant by outputting debug info / default values
#ifdef DEBUG
        std::cerr << "[RegionMetadataManagerImpl] Warning: RegionID " << id.toRaw() << " has no metadata entry!" << std::endl;
#endif
        outMetadata = {};
    }
}

void RegionMetadataManagerImpl::removeRegionMetadata(RegionID id, bool pushDelta) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = metadataMap_.find(id);
    if (it == metadataMap_.end()) return;
    
    RegionMetadata oldMetadata = it->second;
    metadataMap_.erase(it);
    
    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::REGION_METADATA;
        delta.operationType = RegionMetadataOps::REMOVE_METADATA;
        delta.targetId = id.toRaw();
        
        RegionMetadataPayload oldPayload{};
        oldPayload.regionId = id;
        std::memcpy(oldPayload.name, oldMetadata.name, MAX_NAME_LENGTH);
        std::memcpy(oldPayload.comment, oldMetadata.comment, sizeof(oldPayload.comment));
        oldPayload.colorARGB = oldMetadata.colorARGB;
        oldPayload.hasComment = oldMetadata.hasComment;
        
        delta.oldStateSize = sizeof(RegionMetadataPayload);
        std::memcpy(delta.oldState, &oldPayload, sizeof(RegionMetadataPayload));
        
        delta.newStateSize = 0;
        
        history_->pushDelta(delta);
    }
}

bool RegionMetadataManagerImpl::hasRegionMetadata(RegionID id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metadataMap_.find(id) != metadataMap_.end();
}

void RegionMetadataManagerImpl::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    metadataMap_.clear();
}

void RegionMetadataManagerImpl::applyDelta(const ProjectDelta& delta, bool isUndo) {
    std::lock_guard<std::mutex> lock(mutex_);
    RegionID id = RegionID::fromRaw(delta.targetId);
    
    if (delta.operationType == RegionMetadataOps::SET_METADATA) {
        if (isUndo) {
            if (delta.oldStateSize == 0) {
                metadataMap_.erase(id);
            } else {
                RegionMetadataPayload payload{};
                std::memcpy(&payload, delta.oldState, sizeof(RegionMetadataPayload));
                
                RegionMetadata meta{};
                std::memcpy(meta.name, payload.name, MAX_NAME_LENGTH);
                std::memcpy(meta.comment, payload.comment, sizeof(meta.comment));
                meta.colorARGB = payload.colorARGB;
                meta.hasComment = payload.hasComment;
                metadataMap_[id] = meta;
            }
        } else {
            RegionMetadataPayload payload{};
            std::memcpy(&payload, delta.newState, sizeof(RegionMetadataPayload));
            
            RegionMetadata meta{};
            std::memcpy(meta.name, payload.name, MAX_NAME_LENGTH);
            std::memcpy(meta.comment, payload.comment, sizeof(meta.comment));
            meta.colorARGB = payload.colorARGB;
            meta.hasComment = payload.hasComment;
            metadataMap_[id] = meta;
        }
    } else if (delta.operationType == RegionMetadataOps::REMOVE_METADATA) {
        if (isUndo) {
            RegionMetadataPayload payload{};
            std::memcpy(&payload, delta.oldState, sizeof(RegionMetadataPayload));
            
            RegionMetadata meta{};
            std::memcpy(meta.name, payload.name, MAX_NAME_LENGTH);
            std::memcpy(meta.comment, payload.comment, sizeof(meta.comment));
            meta.colorARGB = payload.colorARGB;
            meta.hasComment = payload.hasComment;
            metadataMap_[id] = meta;
        } else {
            metadataMap_.erase(id);
        }
    }
}

void RegionMetadataManagerImpl::setMetadataDirect(const std::unordered_map<RegionID, RegionMetadata>& map) {
    std::lock_guard<std::mutex> lock(mutex_);
    metadataMap_ = map;
}

} // namespace composition
