#include "region_source_manager_impl.h"
#include "source_manager_commands.h"
#include "musical_composition/command_history/icommand_history.h"
#include "DSP nodes/sequencer/audio_sequencer_node.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include <cstring>

namespace composition {

RegionSourceManagerImpl::RegionSourceManagerImpl(ICommandHistory* history)
    : history_(history) {}

SourceID RegionSourceManagerImpl::registerSource(const AudioSourceDescriptor& descriptor, const std::string& filePath) {
    SourceID newId = descriptor.sourceId.isValid() ? descriptor.sourceId : generateNextId();
    SourceState state{ descriptor, filePath, 0 };
    state.descriptor.sourceId = newId;
    sources_[newId.id] = state;

    if (DSP::AudioSequencerFactory::s_butlerThread && !filePath.empty()) {
        DSP::AudioSequencerFactory::s_butlerThread->registerSourcePath(newId.id, filePath.c_str());
    }

    if (history_ && !isApplyingDelta_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::SOURCE_MANAGER;
        delta.operationType = SourceManagerOps::REGISTER_SOURCE;
        delta.targetId = newId.id;

        SourceManagerPayload payload{};
        payload.descriptor = state.descriptor;
        std::strncpy(payload.filePath, filePath.c_str(), sizeof(payload.filePath) - 1);

        delta.newStateSize = sizeof(SourceManagerPayload);
        std::memcpy(delta.newState, &payload, sizeof(SourceManagerPayload));

        history_->pushDelta(delta);
    }
    return newId;
}

bool RegionSourceManagerImpl::getSource(SourceID id, AudioSourceDescriptor& outDescriptor) const {
    auto it = sources_.find(id.id);
    if (it != sources_.end()) {
        outDescriptor = it->second.descriptor;
        return true;
    }
    return false;
}

void RegionSourceManagerImpl::incrementReference(SourceID id) {
    auto it = sources_.find(id.id);
    if (it != sources_.end()) {
        it->second.refCount++;
    }
}

void RegionSourceManagerImpl::decrementReference(SourceID id) {
    auto it = sources_.find(id.id);
    if (it != sources_.end()) {
        if (it->second.refCount > 0) {
            it->second.refCount--;
        }
        
        if (it->second.refCount == 0) {
            std::string path = it->second.filePath;
            AudioSourceDescriptor desc = it->second.descriptor;

            if (DSP::AudioSequencerFactory::s_butlerThread) {
                DSP::AudioSequencerFactory::s_butlerThread->unregisterSourcePath(id.id);
            }
            
            if (history_ && !isApplyingDelta_) {
                ProjectDelta delta{};
                delta.subsystemId = SubsystemID::SOURCE_MANAGER;
                delta.operationType = SourceManagerOps::REGISTER_SOURCE; // Undo of delete is recreate, which is REGISTER_SOURCE!
                delta.targetId = id.id;

                SourceManagerPayload payload{};
                payload.descriptor = desc;
                std::strncpy(payload.filePath, path.c_str(), sizeof(payload.filePath) - 1);

                // For a delete operation, the delta represents the removal of the source.
                // Undoing it should recreate it (newState), redoing it should remove it (oldState/newState erase logic).
                // Let's store the payload in oldState so on undo we restore it!
                delta.oldStateSize = sizeof(SourceManagerPayload);
                std::memcpy(delta.oldState, &payload, sizeof(SourceManagerPayload));

                history_->pushDelta(delta);
            }

            sources_.erase(it);
        }
    }
}

void RegionSourceManagerImpl::applyDelta(const ProjectDelta& delta, bool isUndo) {
    isApplyingDelta_ = true;
    const uint32_t targetId = static_cast<uint32_t>(delta.targetId);
    if (delta.operationType == SourceManagerOps::REGISTER_SOURCE) {
        // REGISTER_SOURCE represents adding a source.
        // Undo: remove it. Redo: restore it.
        // If it was pushed during decrementReference (as a deletion), then:
        // - oldState holds the source payload.
        // - Undo: restore it. Redo: remove it.
        if (delta.oldStateSize > 0 && delta.newStateSize == 0) {
            // This was a deletion delta (pushed in decrementReference).
            if (isUndo) {
                SourceManagerPayload payload{};
                std::memcpy(&payload, delta.oldState, sizeof(SourceManagerPayload));
                
                SourceState state{ payload.descriptor, payload.filePath, 0 };
                sources_[targetId] = state;
                
                if (DSP::AudioSequencerFactory::s_butlerThread && std::strlen(payload.filePath) > 0) {
                    DSP::AudioSequencerFactory::s_butlerThread->registerSourcePath(targetId, payload.filePath);
                }
            } else {
                if (DSP::AudioSequencerFactory::s_butlerThread) {
                    DSP::AudioSequencerFactory::s_butlerThread->unregisterSourcePath(targetId);
                }
                sources_.erase(targetId);
            }
        } else {
            // This was an addition delta (pushed in registerSource).
            if (isUndo) {
                if (DSP::AudioSequencerFactory::s_butlerThread) {
                    DSP::AudioSequencerFactory::s_butlerThread->unregisterSourcePath(targetId);
                }
                sources_.erase(targetId);
            } else {
                SourceManagerPayload payload{};
                std::memcpy(&payload, delta.newState, sizeof(SourceManagerPayload));
                
                SourceState state{ payload.descriptor, payload.filePath, 0 };
                sources_[targetId] = state;
                
                if (DSP::AudioSequencerFactory::s_butlerThread && std::strlen(payload.filePath) > 0) {
                    DSP::AudioSequencerFactory::s_butlerThread->registerSourcePath(targetId, payload.filePath);
                }
            }
        }
    }
    isApplyingDelta_ = false;
}

SourceID RegionSourceManagerImpl::generateNextId() {
    return { ++nextIdCounter_, 1 };
}

std::vector<RegionSourceManagerImpl::SourceEntry> RegionSourceManagerImpl::getAllSources() const {
    std::vector<SourceEntry> entries;
    entries.reserve(sources_.size());
    for (const auto& [id, state] : sources_) {
        entries.push_back({state.descriptor, state.filePath});
    }
    return entries;
}

} // namespace composition
