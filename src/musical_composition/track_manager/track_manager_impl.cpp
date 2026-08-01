#include "track_manager_impl.h"
#include "track_commands.h"
#include "mixer_routing_commands.h"
#include "musical_composition/playlist/playlist_impl.h"
#include "musical_composition/region_manager/iaudio_region_source_manager.h"
#include "musical_composition/midi_sequencer/midi_sequencer_impl.h"
#include "musical_composition/automation/automation_lane_manager_impl.h"
#include "musical_composition/automation/automation_lane_impl.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core audio engine/plugin/iplugin.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "DSP nodes/plugins/insert_plugin_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/audio_input/audio_input_node.h"
#include "DSP nodes/monitor_switch/monitor_switch_node.h"
#include "DSP nodes/latency/latency_node.h"
#include "DSP nodes/tracks/audio_track_node.h"
#include "DSP nodes/tracks/instrument_track_node.h"
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <limits>

namespace composition {

TrackManagerImpl::TrackManagerImpl(
    std::unique_ptr<ITrackPipelineBuilder> builder,
    ICommandHistory* history,
    IAudioRegionSourceManager* sourceManager,
    IDSPKernel* kernel,
    Layer2::IMutationBridge* mutationBridge,
    Layer3::IPluginManager* pluginManager,
    NodeID masterChannelStripNode,
    NodeID masterPluginSlotNode,
    NodeID masterLatencyNode,
    DSP::LatencyFactory* latencyFactory)
    : builder_(std::move(builder))
    , commandHistory_(history)
    , sourceManager_(sourceManager)
    , kernel_(kernel)
    , mutationBridge_(mutationBridge)
    , pluginManager_(pluginManager)
    , masterChannelStripNode_(masterChannelStripNode)
    , masterPluginSlotNode_(masterPluginSlotNode)
    , masterLatencyNode_(masterLatencyNode)
    , latencyFactory_(latencyFactory)
    , rtSequencerListA_(std::make_unique<RTSequencerList>())
    , rtSequencerListB_(std::make_unique<RTSequencerList>())
{
    rtSequencersActive_.store(rtSequencerListA_.get(), std::memory_order_release);

    // Initialize Default Arrangement
    activeArrangementId_ = { 1 };
    ArrangementInfo defArr;
    defArr.id = activeArrangementId_;
    std::strcpy(defArr.name, "Default Arrangement");
    defArr.isActive = true;
    defArr.padding[0] = 0;
    defArr.padding[1] = 0;
    defArr.padding[2] = 0;
    arrangements_.push_back(defArr);
    nextArrangementIdCounter_ = 2;
}

TrackManagerImpl::~TrackManagerImpl() {
    for (auto& [id, track] : tracks_) {
        if (track.instrument.pluginInstance) {
            delete static_cast<Layer3::IPlugin*>(track.instrument.pluginInstance);
            track.instrument.pluginInstance = nullptr;
        }
        for (int s = 0; s < 8; ++s) {
            if (track.plugins[s].pluginInstance) {
                delete static_cast<Layer3::IPlugin*>(track.plugins[s].pluginInstance);
                track.plugins[s].pluginInstance = nullptr;
            }
        }
    }
}

TrackID TrackManagerImpl::createTrack(const TrackCreateInfo& info) {
    TrackCreateInfo localInfo = info;
    TrackID id = createTrackInternal(localInfo);

    // 3. Generate Undo/Redo Delta
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::CREATE_TRACK;
        delta.targetId = handleToUint64(id);
        delta.newStateSize = sizeof(TrackCreateInfo);
        
        TrackCreateInfo fullInfo = localInfo;
        fullInfo.trackId = id;
        std::memcpy(delta.newState, &fullInfo, sizeof(TrackCreateInfo));
        commandHistory_->pushDelta(delta);
    }
    return id;
}

TrackID TrackManagerImpl::createTrackInternal(const TrackCreateInfo& info) {
    TrackID newId = info.trackId.isValid() ? info.trackId : generateNextId();
    if (newId.id > nextIdCounter_) {
        nextIdCounter_ = newId.id;
    }

    TrackState& state = tracks_[newId.id];
    state.recordingStartSample = std::make_unique<std::atomic<uint64_t>>(std::numeric_limits<uint64_t>::max());
    state.info = info;
    state.info.trackId = newId;
    state.info.recordingStartSample = state.recordingStartSample.get();

    // 1. Delegate DSP pipeline construction downward
    state.pipeline = builder_->buildPipeline(state.info, kernel_);

    // 2. Instantiate sibling Layer 5 subsystems based on Track Type
    // 2. Pre-initialize the active arrangement state for this track
    uint32_t arrId = activeArrangementId_.isValid() ? activeArrangementId_.id : 1;
    ArrangementState arrState;
    if (state.info.type == TrackType::AUDIO || state.info.type == TrackType::MIDI || 
        state.info.type == TrackType::INSTRUMENT) {
        arrState.playlist = std::make_unique<PlaylistImpl>(newId, commandHistory_, sourceManager_);
    }
    if (state.info.type == TrackType::MIDI || state.info.type == TrackType::INSTRUMENT) {
        arrState.midiSequencer = std::make_unique<MIDISequencerImpl>(newId, commandHistory_);
        arrState.midiSequencer->setTargetNodeId(state.pipeline.sourceNode);
    }
    arrState.automationManager = std::make_unique<AutomationLaneManagerImpl>(newId, commandHistory_);
    state.arrangements[arrId] = std::move(arrState);
    
    syncRTSequencerList();
    compileTimelineSnapshot();
    
    return newId;
}

void TrackManagerImpl::deleteTrack(TrackID id) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    // 1. Generate Undo/Redo Delta
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::DELETE_TRACK;
        delta.targetId = handleToUint64(id);
        delta.oldStateSize = sizeof(TrackCreateInfo);
        std::memcpy(delta.oldState, &it->second.info, sizeof(TrackCreateInfo));
        commandHistory_->pushDelta(delta);
    }

    deleteTrackInternal(id);
}

void TrackManagerImpl::deleteTrackInternal(TrackID id) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    // Clear active sidechain connections where deleted track is target or source
    for (auto& [tid, tState] : tracks_) {
        for (uint32_t s = 0; s < 8; ++s) {
            if (tState.sidechains[s].isEnabled) {
                if (tid == id.id || tState.sidechains[s].sourceTrackId == id) {
                    clearTrackSidechainRouting(tState.info.trackId, s);
                }
            }
        }
    }

    // Delete plugin and instrument instances to prevent leaks
    if (it->second.instrument.pluginInstance) {
        delete static_cast<Layer3::IPlugin*>(it->second.instrument.pluginInstance);
        it->second.instrument.pluginInstance = nullptr;
    }
    for (int s = 0; s < 8; ++s) {
        if (it->second.plugins[s].pluginInstance) {
            delete static_cast<Layer3::IPlugin*>(it->second.plugins[s].pluginInstance);
            it->second.plugins[s].pluginInstance = nullptr;
        }
    }

    // Delegate DSP teardown
    builder_->destroyPipeline(it->second.pipeline, kernel_);

    // Erase from map
    tracks_.erase(it);

    syncRTSequencerList();
    compileTimelineSnapshot();
}

void TrackManagerImpl::renameTrack(TrackID id, uint32_t newNameId) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    RenameTrackPayload payload{};
    payload.oldNameId = it->second.info.nameId;
    payload.newNameId = newNameId;

    renameTrackInternal(id, newNameId);
    
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::RENAME_TRACK;
        delta.targetId = handleToUint64(id);
        delta.oldStateSize = sizeof(RenameTrackPayload);
        std::memcpy(delta.oldState, &payload, sizeof(RenameTrackPayload));
        delta.newStateSize = sizeof(RenameTrackPayload);
        std::memcpy(delta.newState, &payload, sizeof(RenameTrackPayload));
        
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackComments(TrackID id, uint32_t commentsId) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    SetTrackCommentsPayload payload{};
    payload.oldCommentsId = it->second.info.commentsId;
    payload.newCommentsId = commentsId;

    setTrackCommentsInternal(id, commentsId);

    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::SET_COMMENTS;
        delta.targetId = handleToUint64(id);
        delta.oldStateSize = sizeof(SetTrackCommentsPayload);
        std::memcpy(delta.oldState, &payload, sizeof(SetTrackCommentsPayload));
        delta.newStateSize = sizeof(SetTrackCommentsPayload);
        std::memcpy(delta.newState, &payload, sizeof(SetTrackCommentsPayload));
        
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackOutputRouting(TrackID id, TrackID destinationTrackId) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    SetTrackOutputRoutingPayload payload{};
    payload.oldOutputTargetTrackId = it->second.info.outputTargetTrackId;
    payload.newOutputTargetTrackId = destinationTrackId;

    setTrackOutputRoutingInternal(id, destinationTrackId);

    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::SET_OUTPUT_ROUTING;
        delta.targetId = handleToUint64(id);
        delta.oldStateSize = sizeof(SetTrackOutputRoutingPayload);
        std::memcpy(delta.oldState, &payload, sizeof(SetTrackOutputRoutingPayload));
        delta.newStateSize = sizeof(SetTrackOutputRoutingPayload);
        std::memcpy(delta.newState, &payload, sizeof(SetTrackOutputRoutingPayload));
        
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::moveTrack(TrackID id, uint32_t newIndexPosition, TrackID newParentFolderId) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    MoveTrackPayload oldPayload{};
    oldPayload.newIndexPosition = it->second.indexPosition;
    oldPayload.newParentFolderId = it->second.parentFolderId;

    MoveTrackPayload newPayload{};
    newPayload.newIndexPosition = newIndexPosition;
    newPayload.newParentFolderId = newParentFolderId;

    moveTrackInternal(id, newIndexPosition, newParentFolderId);
    
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::MOVE_TRACK;
        delta.targetId = handleToUint64(id);
        
        delta.oldStateSize = sizeof(MoveTrackPayload);
        std::memcpy(delta.oldState, &oldPayload, sizeof(MoveTrackPayload));
        
        delta.newStateSize = sizeof(MoveTrackPayload);
        std::memcpy(delta.newState, &newPayload, sizeof(MoveTrackPayload));
        
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackColor(TrackID id, uint32_t newColorARGB) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    SetTrackColorPayload payload{};
    payload.oldColorARGB = it->second.info.colorARGB;
    payload.newColorARGB = newColorARGB;

    setTrackColorInternal(id, newColorARGB);
    
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::SET_COLOR;
        delta.targetId = handleToUint64(id);
        delta.oldStateSize = sizeof(SetTrackColorPayload);
        std::memcpy(delta.oldState, &payload, sizeof(SetTrackColorPayload));
        delta.newStateSize = sizeof(SetTrackColorPayload);
        std::memcpy(delta.newState, &payload, sizeof(SetTrackColorPayload));
        
        commandHistory_->pushDelta(delta);
    }
}

IPlaylist* TrackManagerImpl::getPlaylist(TrackID id) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return nullptr;
    
    uint32_t arrId = activeArrangementId_.isValid() ? activeArrangementId_.id : 1;
    auto arrIt = it->second.arrangements.find(arrId);
    if (arrIt != it->second.arrangements.end() && arrIt->second.playlist) {
        return arrIt->second.playlist.get();
    }
    
    // Lazily create if appropriate for TrackType
    if (it->second.info.type == TrackType::AUDIO || it->second.info.type == TrackType::MIDI || 
        it->second.info.type == TrackType::INSTRUMENT) {
        auto& arrState = it->second.arrangements[arrId];
        if (!arrState.playlist) {
            arrState.playlist = std::make_unique<PlaylistImpl>(id, commandHistory_, sourceManager_);
        }
        return arrState.playlist.get();
    }
    return nullptr;
}



IMIDISequencer* TrackManagerImpl::getMIDISequencer(TrackID id) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return nullptr;
    
    uint32_t arrId = activeArrangementId_.isValid() ? activeArrangementId_.id : 1;
    auto arrIt = it->second.arrangements.find(arrId);
    if (arrIt != it->second.arrangements.end() && arrIt->second.midiSequencer) {
        return arrIt->second.midiSequencer.get();
    }
    
    // Lazily create if appropriate for TrackType
    if (it->second.info.type == TrackType::MIDI || it->second.info.type == TrackType::INSTRUMENT) {
        auto& arrState = it->second.arrangements[arrId];
        if (!arrState.midiSequencer) {
            arrState.midiSequencer = std::make_unique<MIDISequencerImpl>(id, commandHistory_);
            arrState.midiSequencer->setTargetNodeId(it->second.pipeline.sourceNode);
        }
        return arrState.midiSequencer.get();
    }
    return nullptr;
}

IAutomationLaneManager* TrackManagerImpl::getAutomationManager(TrackID id) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return nullptr;
    
    uint32_t arrId = activeArrangementId_.isValid() ? activeArrangementId_.id : 1;
    auto arrIt = it->second.arrangements.find(arrId);
    if (arrIt != it->second.arrangements.end() && arrIt->second.automationManager) {
        return arrIt->second.automationManager.get();
    }
    
    // Lazily create (automation is always appropriate)
    auto& arrState = it->second.arrangements[arrId];
    if (!arrState.automationManager) {
        arrState.automationManager = std::make_unique<AutomationLaneManagerImpl>(id, commandHistory_);
    }
    return arrState.automationManager.get();
}

TrackPipelineDescriptor TrackManagerImpl::getPipelineDescriptor(TrackID id) const {
    if (id.id == 0 && id.generation == 0) {
        TrackPipelineDescriptor desc{};
        desc.trackNode = masterChannelStripNode_;
        desc.latencySamples = 0;
        return desc;
    }
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) return it->second.pipeline;
    return {};
}

NodeID TrackManagerImpl::getTrackOutputNode(TrackID id) const {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) return it->second.pipeline.trackNode;
    return {};
}

TrackID TrackManagerImpl::generateNextId() {
    TrackID id;
    id.id = ++nextIdCounter_;
    id.generation = 1;
    return id;
}

void TrackManagerImpl::applyDelta(const ProjectDelta& delta, bool isUndo) {
    if (delta.subsystemId == SubsystemID::TRACK_MANAGER) {
        switch (delta.operationType) {
            case TrackOps::CREATE_TRACK: {
                TrackCreateInfo info;
                std::memcpy(&info, isUndo ? delta.oldState : delta.newState, sizeof(TrackCreateInfo));
                if (isUndo) {
                    deleteTrackInternal(uint64ToHandle<TrackID>(delta.targetId));
                } else {
                    createTrackInternal(info);
                }
                break;
            }
            case TrackOps::DELETE_TRACK: {
                TrackCreateInfo info;
                std::memcpy(&info, isUndo ? delta.oldState : delta.newState, sizeof(TrackCreateInfo));
                if (isUndo) {
                    createTrackInternal(info);
                } else {
                    deleteTrackInternal(uint64ToHandle<TrackID>(delta.targetId));
                }
                break;
            }
            case TrackOps::RENAME_TRACK: {
                RenameTrackPayload payload;
                std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(RenameTrackPayload));
                renameTrackInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? payload.oldNameId : payload.newNameId);
                break;
            }
            case TrackOps::MOVE_TRACK: {
                MoveTrackPayload payload;
                std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(MoveTrackPayload));
                moveTrackInternal(uint64ToHandle<TrackID>(delta.targetId), payload.newIndexPosition, payload.newParentFolderId);
                break;
            }
            case TrackOps::SET_COLOR: {
                SetTrackColorPayload payload;
                std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(SetTrackColorPayload));
                setTrackColorInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? payload.oldColorARGB : payload.newColorARGB);
                break;
            }
            case TrackOps::SET_RECORD_ARMED: {
                SetRecordArmedPayload payload;
                std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(SetRecordArmedPayload));
                setTrackRecordArmedInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? payload.oldArmed : payload.newArmed);
                break;
            }
            case TrackOps::SET_INPUT_MONITORING: {
                SetInputMonitoringPayload payload;
                std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(SetInputMonitoringPayload));
                setTrackInputMonitoringInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? payload.oldEnabled : payload.newEnabled);
                break;
            }
            case TrackOps::SET_TYPE: {
                SetTrackTypePayload payload;
                std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(SetTrackTypePayload));
                setTrackTypeInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? payload.oldType : payload.newType);
                break;
            }
            case TrackOps::SET_LOCKED: {
                SetTrackLockedPayload payload;
                std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(SetTrackLockedPayload));
                setTrackLockedInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? payload.oldLocked : payload.newLocked);
                break;
            }
            case TrackOps::SET_COMMENTS: {
                SetTrackCommentsPayload payload;
                std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(SetTrackCommentsPayload));
                setTrackCommentsInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? payload.oldCommentsId : payload.newCommentsId);
                break;
            }
            case TrackOps::SET_OUTPUT_ROUTING: {
                SetTrackOutputRoutingPayload payload;
                std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(SetTrackOutputRoutingPayload));
                setTrackOutputRoutingInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? payload.oldOutputTargetTrackId : payload.newOutputTargetTrackId);
                break;
            }
        }
    } else if (delta.subsystemId == SubsystemID::MIXER_ROUTING) {
        switch (delta.operationType) {
            case MixerRoutingOps::SET_FADER_GAIN: {
                MixerParamPayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(MixerParamPayload));
                setTrackFaderGainInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? p.oldValue : p.newValue);
                break;
            }
            case MixerRoutingOps::SET_PAN: {
                MixerParamPayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(MixerParamPayload));
                setTrackPanInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? p.oldValue : p.newValue);
                break;
            }
            case MixerRoutingOps::SET_MUTE: {
                MixerParamPayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(MixerParamPayload));
                setTrackMuteInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? (p.oldValue > 0.5f) : (p.newValue > 0.5f));
                break;
            }
            case MixerRoutingOps::SET_SOLO: {
                MixerParamPayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(MixerParamPayload));
                setTrackSoloInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? (p.oldValue > 0.5f) : (p.newValue > 0.5f));
                break;
            }
            case MixerRoutingOps::SET_SEND_GAIN: {
                SendRoutingPayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(SendRoutingPayload));
                setTrackSendGainInternal(uint64ToHandle<TrackID>(delta.targetId), p.isPreFader, p.sendIndex, isUndo ? p.oldValue : p.newValue);
                break;
            }
            case MixerRoutingOps::SET_SEND_PAN: {
                SendRoutingPayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(SendRoutingPayload));
                setTrackSendPanInternal(uint64ToHandle<TrackID>(delta.targetId), p.isPreFader, p.sendIndex, isUndo ? p.oldValue : p.newValue);
                break;
            }
            case MixerRoutingOps::SET_SEND_ENABLED: {
                SendRoutingPayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(SendRoutingPayload));
                setTrackSendEnabledInternal(uint64ToHandle<TrackID>(delta.targetId), p.isPreFader, p.sendIndex, isUndo ? (p.oldValue > 0.5f) : (p.newValue > 0.5f));
                break;
            }
            case MixerRoutingOps::SET_SEND_DEST: {
                SendRoutingPayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(SendRoutingPayload));
                setTrackSendDestinationInternal(uint64ToHandle<TrackID>(delta.targetId), p.isPreFader, p.sendIndex, isUndo ? p.oldDestNodeId : p.newDestNodeId);
                break;
            }
            case MixerRoutingOps::SET_AUDIO_INPUT: {
                break;
            }
            case MixerRoutingOps::INSERT_PLUGIN: {
                PluginLifecyclePayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(PluginLifecyclePayload));
                if (isUndo) {
                    removeTrackPluginInternal(uint64ToHandle<TrackID>(delta.targetId), p.slotIndex);
                } else {
                    insertTrackPluginInternal(uint64ToHandle<TrackID>(delta.targetId), p.slotIndex, p.pluginId, p.stateId);
                }
                break;
            }
            case MixerRoutingOps::REMOVE_PLUGIN: {
                PluginLifecyclePayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(PluginLifecyclePayload));
                if (isUndo) {
                    insertTrackPluginInternal(uint64ToHandle<TrackID>(delta.targetId), p.slotIndex, p.pluginId, p.stateId);
                } else {
                    removeTrackPluginInternal(uint64ToHandle<TrackID>(delta.targetId), p.slotIndex);
                }
                break;
            }
            case MixerRoutingOps::SET_PLUGIN_BYPASS: {
                PluginLifecyclePayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(PluginLifecyclePayload));
                setTrackPluginBypassedInternal(uint64ToHandle<TrackID>(delta.targetId), p.slotIndex, isUndo ? p.oldBypassed : p.newBypassed);
                break;
            }
            case MixerRoutingOps::INSERT_INSTRUMENT: {
                PluginLifecyclePayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(PluginLifecyclePayload));
                if (isUndo) {
                    removeTrackInstrumentInternal(uint64ToHandle<TrackID>(delta.targetId));
                } else {
                    insertTrackInstrumentInternal(uint64ToHandle<TrackID>(delta.targetId), p.pluginId, p.stateId);
                }
                break;
            }
            case MixerRoutingOps::REMOVE_INSTRUMENT: {
                PluginLifecyclePayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(PluginLifecyclePayload));
                if (isUndo) {
                    insertTrackInstrumentInternal(uint64ToHandle<TrackID>(delta.targetId), p.pluginId, p.stateId);
                } else {
                    removeTrackInstrumentInternal(uint64ToHandle<TrackID>(delta.targetId));
                }
                break;
            }
            case MixerRoutingOps::SET_INSTRUMENT_BYPASS: {
                PluginLifecyclePayload p;
                std::memcpy(&p, isUndo ? delta.oldState : delta.newState, sizeof(PluginLifecyclePayload));
                setTrackInstrumentBypassedInternal(uint64ToHandle<TrackID>(delta.targetId), isUndo ? p.oldBypassed : p.newBypassed);
                break;
            }
        }
    }
    syncRTSequencerList();
    compileTimelineSnapshot();
}

void TrackManagerImpl::renameTrackInternal(TrackID id, uint32_t newNameId) {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) it->second.info.nameId = newNameId;
}

void TrackManagerImpl::setTrackCommentsInternal(TrackID id, uint32_t commentsId) {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) it->second.info.commentsId = commentsId;
}

void TrackManagerImpl::setTrackOutputRoutingInternal(TrackID id, TrackID destinationTrackId) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    TrackID oldTarget = it->second.info.outputTargetTrackId;

    // Disconnect old route and connect new route
    NodeID trackNode = it->second.pipeline.trackNode;
    if (trackNode.isValid() && mutationBridge_) {
        uint32_t trackNodeType = (it->second.info.type == composition::TrackType::INSTRUMENT) ? 
            DSP::NODE_TYPE_INSTRUMENT_TRACK : DSP::NODE_TYPE_AUDIO_TRACK;

        // 1. Determine effective old output destination node & type
        NodeID oldDestNodeId = NodeID::invalid();
        uint32_t oldDestType = DSP::NODE_TYPE_AUDIO_TRACK;
        TrackID effectiveOldTarget = oldTarget.isValid() ? oldTarget : it->second.parentFolderId;
        if (effectiveOldTarget.isValid()) {
            auto oldIt = tracks_.find(effectiveOldTarget.id);
            if (oldIt != tracks_.end()) {
                oldDestNodeId = oldIt->second.pipeline.trackNode;
                oldDestType = (oldIt->second.info.type == composition::TrackType::INSTRUMENT) ?
                    DSP::NODE_TYPE_INSTRUMENT_TRACK : DSP::NODE_TYPE_AUDIO_TRACK;
            }
        } else {
            oldDestNodeId = masterChannelStripNode_;
            oldDestType = DSP::NODE_TYPE_BUS;
        }

        // 2. Determine effective new output destination node & type
        NodeID newDestNodeId = NodeID::invalid();
        uint32_t newDestType = DSP::NODE_TYPE_AUDIO_TRACK;
        TrackID effectiveNewTarget = destinationTrackId.isValid() ? destinationTrackId : it->second.parentFolderId;
        if (effectiveNewTarget.isValid()) {
            auto newIt = tracks_.find(effectiveNewTarget.id);
            if (newIt != tracks_.end()) {
                newDestNodeId = newIt->second.pipeline.trackNode;
                newDestType = (newIt->second.info.type == composition::TrackType::INSTRUMENT) ?
                    DSP::NODE_TYPE_INSTRUMENT_TRACK : DSP::NODE_TYPE_AUDIO_TRACK;
            }
        } else {
            newDestNodeId = masterChannelStripNode_;
            newDestType = DSP::NODE_TYPE_BUS;
        }

        uint32_t oldDestPortBase = (oldDestType == DSP::NODE_TYPE_BUS) ? 0 : TRACK_INPUT_PLAYBACK_PORT_BASE;
        uint32_t newDestPortBase = (newDestType == DSP::NODE_TYPE_BUS) ? 0 : TRACK_INPUT_PLAYBACK_PORT_BASE;

        // Disconnect old route if it differs from new route
        if (oldDestNodeId.isValid() && (oldDestNodeId != newDestNodeId || oldDestPortBase != newDestPortBase)) {
            SystemMutation discL{};
            discL.type = Layer2::MutationType::NODE_DISCONNECT;
            discL.priority = 128;
            discL.connection.sourceNodeIndex = (trackNodeType << 16) | (trackNode.id & 0xFFFF);
            discL.connection.sourcePort = 0;
            discL.connection.destNodeIndex = (oldDestType << 16) | (oldDestNodeId.id & 0xFFFF);
            discL.connection.destPort = oldDestPortBase + 0;
            discL.connection.gain = 1.0f;
            mutationBridge_->pushMutation(discL);

            SystemMutation discR{};
            discR.type = Layer2::MutationType::NODE_DISCONNECT;
            discR.priority = 128;
            discR.connection.sourceNodeIndex = (trackNodeType << 16) | (trackNode.id & 0xFFFF);
            discR.connection.sourcePort = 1;
            discR.connection.destNodeIndex = (oldDestType << 16) | (oldDestNodeId.id & 0xFFFF);
            discR.connection.destPort = oldDestPortBase + 1;
            discR.connection.gain = 1.0f;
            mutationBridge_->pushMutation(discR);
        }

        // Connect new route if it differs from old route
        if (newDestNodeId.isValid() && (oldDestNodeId != newDestNodeId || oldDestPortBase != newDestPortBase)) {
            SystemMutation connL{};
            connL.type = Layer2::MutationType::NODE_CONNECT;
            connL.priority = 128;
            connL.connection.sourceNodeIndex = (trackNodeType << 16) | (trackNode.id & 0xFFFF);
            connL.connection.sourcePort = 0;
            connL.connection.destNodeIndex = (newDestType << 16) | (newDestNodeId.id & 0xFFFF);
            connL.connection.destPort = newDestPortBase + 0;
            connL.connection.gain = 1.0f;
            mutationBridge_->pushMutation(connL);

            SystemMutation connR{};
            connR.type = Layer2::MutationType::NODE_CONNECT;
            connR.priority = 128;
            connR.connection.sourceNodeIndex = (trackNodeType << 16) | (trackNode.id & 0xFFFF);
            connR.connection.sourcePort = 1;
            connR.connection.destNodeIndex = (newDestType << 16) | (newDestNodeId.id & 0xFFFF);
            connR.connection.destPort = newDestPortBase + 1;
            connR.connection.gain = 1.0f;
            mutationBridge_->pushMutation(connR);
        }
    }

    it->second.info.outputTargetTrackId = destinationTrackId;
}

void TrackManagerImpl::moveTrackInternal(TrackID id, uint32_t newIndexPosition, TrackID newParentFolderId) {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        it->second.indexPosition = newIndexPosition;
        it->second.parentFolderId = newParentFolderId;
    }
}

void TrackManagerImpl::setTrackColorInternal(TrackID id, uint32_t newColorARGB) {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) it->second.info.colorARGB = newColorARGB;
}

void TrackManagerImpl::setTrackRecordArmedInternal(TrackID id, bool armed) {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) it->second.info.isRecordArmed = armed;
}

void TrackManagerImpl::setTrackInputMonitoringInternal(TrackID id, bool enabled) {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) it->second.info.isInputMonitoring = enabled;
}

void TrackManagerImpl::setTrackTypeInternal(TrackID id, TrackType type) {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        it->second.info.type = type;
        
        uint32_t arrId = activeArrangementId_.isValid() ? activeArrangementId_.id : 1;
        auto& arrState = it->second.arrangements[arrId];
        
        // Playlist is for AUDIO, MIDI, and INSTRUMENT tracks
        if (type == TrackType::AUDIO || type == TrackType::MIDI || 
            type == TrackType::INSTRUMENT) {
            if (!arrState.playlist) {
                arrState.playlist = std::make_unique<PlaylistImpl>(id, commandHistory_, sourceManager_);
            }
        } else {
            arrState.playlist.reset();
        }

        // MIDI Sequencer is for MIDI and INSTRUMENT tracks
        if (type == TrackType::MIDI || type == TrackType::INSTRUMENT) {
            if (!arrState.midiSequencer) {
                arrState.midiSequencer = std::make_unique<MIDISequencerImpl>(id, commandHistory_);
                arrState.midiSequencer->setTargetNodeId(it->second.pipeline.sourceNode);
            }
        } else {
            arrState.midiSequencer.reset();
        }
    }
}

void TrackManagerImpl::setTrackRecordArmed(TrackID id, bool armed) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    SetRecordArmedPayload payload{};
    payload.oldArmed = it->second.info.isRecordArmed;
    payload.newArmed = armed;

    setTrackRecordArmedInternal(id, armed);
    syncRTSequencerList();

    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::SET_RECORD_ARMED;
        delta.targetId = handleToUint64(id);
        delta.oldStateSize = sizeof(SetRecordArmedPayload);
        std::memcpy(delta.oldState, &payload, sizeof(SetRecordArmedPayload));
        delta.newStateSize = sizeof(SetRecordArmedPayload);
        std::memcpy(delta.newState, &payload, sizeof(SetRecordArmedPayload));

        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackInputMonitoring(TrackID id, bool enabled) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    SetInputMonitoringPayload payload{};
    payload.oldEnabled = it->second.info.isInputMonitoring;
    payload.newEnabled = enabled;

    setTrackInputMonitoringInternal(id, enabled);
    syncRTSequencerList();

    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::SET_INPUT_MONITORING;
        delta.targetId = handleToUint64(id);
        delta.oldStateSize = sizeof(SetInputMonitoringPayload);
        std::memcpy(delta.oldState, &payload, sizeof(SetInputMonitoringPayload));
        delta.newStateSize = sizeof(SetInputMonitoringPayload);
        std::memcpy(delta.newState, &payload, sizeof(SetInputMonitoringPayload));

        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackType(TrackID id, TrackType type) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    SetTrackTypePayload payload{};
    payload.oldType = it->second.info.type;
    payload.newType = type;

    setTrackTypeInternal(id, type);
    syncRTSequencerList();
    compileTimelineSnapshot();

    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::SET_TYPE;
        delta.targetId = handleToUint64(id);
        delta.oldStateSize = sizeof(SetTrackTypePayload);
        std::memcpy(delta.oldState, &payload, sizeof(SetTrackTypePayload));
        delta.newStateSize = sizeof(SetTrackTypePayload);
        std::memcpy(delta.newState, &payload, sizeof(SetTrackTypePayload));

        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackTakesExpanded(TrackID id, bool expanded) {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        it->second.info.isTakesExpanded = expanded;
    }
}

void TrackManagerImpl::setTrackLockedInternal(TrackID id, bool locked) {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        it->second.locked = locked;
    }
}

void TrackManagerImpl::setTrackLocked(TrackID id, bool locked) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;

    SetTrackLockedPayload payload{};
    payload.oldLocked = it->second.locked;
    payload.newLocked = locked;

    setTrackLockedInternal(id, locked);

    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::TRACK_MANAGER;
        delta.operationType = TrackOps::SET_LOCKED;
        delta.targetId = handleToUint64(id);
        delta.oldStateSize = sizeof(SetTrackLockedPayload);
        std::memcpy(delta.oldState, &payload, sizeof(SetTrackLockedPayload));
        delta.newStateSize = sizeof(SetTrackLockedPayload);
        std::memcpy(delta.newState, &payload, sizeof(SetTrackLockedPayload));

        commandHistory_->pushDelta(delta);
    }
}

bool TrackManagerImpl::isTrackLocked(TrackID id) const {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        return it->second.locked;
    }
    return false;
}



std::vector<TrackID> TrackManagerImpl::getAllTrackIDs() const {
    std::vector<TrackID> ids;
    ids.reserve(tracks_.size());
    for (auto const& [key, state] : tracks_) {
        TrackID id;
        id.id = key;
        id.generation = 1;
        ids.push_back(id);
    }
    return ids;
}

bool TrackManagerImpl::getTrackInfo(TrackID id, TrackCreateInfo& outInfo) const {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        outInfo = it->second.info;
        return true;
    }
    return false;
}

uint32_t TrackManagerImpl::getTrackIndexPosition(TrackID id) const {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        return it->second.indexPosition;
    }
    return 0;
}

TrackID TrackManagerImpl::getTrackParentFolderId(TrackID id) const {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        return it->second.parentFolderId;
    }
    return TrackID{0, 0};
}

std::atomic<uint64_t>* TrackManagerImpl::getRecordingStartSample(TrackID id) {
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        return it->second.recordingStartSample.get();
    }
    return nullptr;
}

void TrackManagerImpl::syncRTSequencerList() {
    RTSequencerList* active = rtSequencersActive_.load(std::memory_order_relaxed);
    RTSequencerList* inactive = (active == rtSequencerListA_.get()) ? rtSequencerListB_.get() : rtSequencerListA_.get();
    
    inactive->count = 0;
    for (auto& pair : tracks_) {
        if (inactive->count >= 1024) break;
        
        TrackID tid = { pair.first, 1 };
        inactive->midiSequencers[inactive->count] = getMIDISequencer(tid); // can be nullptr
        inactive->automationManagers[inactive->count] = getAutomationManager(tid); // can be nullptr
        inactive->isRecordArmed[inactive->count] = pair.second.info.isRecordArmed;
        inactive->isInputMonitoring[inactive->count] = pair.second.info.isInputMonitoring;
        inactive->targetNodeIds[inactive->count] = pair.second.pipeline.instrumentSlotNode.isValid() ? 
                                                    pair.second.pipeline.instrumentSlotNode : 
                                                    pair.second.pipeline.sourceNode;
        inactive->count++;
    }
    
    rtSequencersActive_.store(inactive, std::memory_order_release);
}

void TrackManagerImpl::renderMIDIPlayback(
    uint64_t /*startSample*/,
    uint32_t /*numSamples*/,
    bool /*loopEnabled*/,
    uint64_t /*loopStart*/,
    uint64_t /*loopEnd*/,
    Layer2::IEventQueue* /*eventQueue*/,
    bool /*isPlaying*/
) {
    RTSequencerList* active = rtSequencersActive_.load(std::memory_order_acquire);
    if (!active) return;
    
    NodeID liveTargets[128];
    uint32_t liveTargetCount = 0;
    
    for (uint32_t i = 0; i < active->count; ++i) {
        if (active->isRecordArmed[i] || active->isInputMonitoring[i]) {
            if (liveTargetCount < 128 && active->targetNodeIds[i].isValid()) {
                liveTargets[liveTargetCount++] = active->targetNodeIds[i];
            }
        }
    }
    
    if (kernel_) {
        kernel_->setLiveMIDITargets(liveTargets, liveTargetCount);
    }
}

uint32_t TrackManagerImpl::getNotesInClip(ClipID clipId, ::MIDINote* outNotes, uint32_t maxNotes) const {
    RTSequencerList* active = rtSequencersActive_.load(std::memory_order_acquire);
    if (!active) return 0;

    for (uint32_t i = 0; i < active->count; ++i) {
        if (auto* seq = active->midiSequencers[i]) {
            static constexpr uint32_t TEMP_MAX = 512;
            composition::MIDINote tempNotes[TEMP_MAX];
            uint32_t limit = (maxNotes < TEMP_MAX) ? maxNotes : TEMP_MAX;
            uint32_t count = seq->getNotesInClip(clipId, tempNotes, limit);
            if (count > 0) {
                for (uint32_t j = 0; j < count; ++j) {
                    outNotes[j].noteId = tempNotes[j].noteId.id;
                    outNotes[j].pitch = tempNotes[j].pitch;
                    outNotes[j].velocity = tempNotes[j].velocity;
                    outNotes[j].offsetSample = tempNotes[j].offsetSample;
                    outNotes[j].durationSample = tempNotes[j].durationSample;
                    outNotes[j].channel = tempNotes[j].channel;
                }
                return count;
            }
        }
    }
    return 0;
}

uint32_t TrackManagerImpl::getCCPointsInClip(ClipID clipId, ::MIDICCPoint* outPoints, uint32_t maxPoints) const {
    RTSequencerList* active = rtSequencersActive_.load(std::memory_order_acquire);
    if (!active) return 0;

    for (uint32_t i = 0; i < active->count; ++i) {
        if (auto* seq = active->midiSequencers[i]) {
            static constexpr uint32_t TEMP_MAX = 256;
            composition::MIDICCPoint tempCC[TEMP_MAX];
            uint32_t limit = (maxPoints < TEMP_MAX) ? maxPoints : TEMP_MAX;
            uint32_t count = seq->getCCPointsInClip(clipId, tempCC, limit);
            if (count > 0) {
                for (uint32_t j = 0; j < count; ++j) {
                    outPoints[j].samplePosition = tempCC[j].samplePosition;
                    outPoints[j].channel = tempCC[j].channel;
                    outPoints[j].controllerNumber = tempCC[j].controllerNumber;
                    outPoints[j].value = tempCC[j].value;
                    std::memset(outPoints[j].padding, 0, sizeof(outPoints[j].padding));
                }
                return count;
            }
        }
    }
    return 0;
}

uint32_t TrackManagerImpl::getPitchPointsInClip(ClipID clipId, ::MIDIPitchPoint* outPoints, uint32_t maxPoints) const {
    RTSequencerList* active = rtSequencersActive_.load(std::memory_order_acquire);
    if (!active) return 0;

    for (uint32_t i = 0; i < active->count; ++i) {
        if (auto* seq = active->midiSequencers[i]) {
            static constexpr uint32_t TEMP_MAX = 256;
            composition::MIDIPitchPoint tempPB[TEMP_MAX];
            uint32_t limit = (maxPoints < TEMP_MAX) ? maxPoints : TEMP_MAX;
            uint32_t count = seq->getPitchPointsInClip(clipId, tempPB, limit);
            if (count > 0) {
                for (uint32_t j = 0; j < count; ++j) {
                    outPoints[j].samplePosition = tempPB[j].samplePosition;
                    outPoints[j].channel = tempPB[j].channel;
                    outPoints[j].value = tempPB[j].value;
                    std::memset(outPoints[j].padding, 0, sizeof(outPoints[j].padding));
                }
                return count;
            }
        }
    }
    return 0;
}

void TrackManagerImpl::setProjectSampleRate(uint32_t sampleRate) {
    projectSampleRate_ = sampleRate;
    for (auto& pair : tracks_) {
        for (auto& arrPair : pair.second.arrangements) {
            if (arrPair.second.playlist) {
                arrPair.second.playlist->setProjectSampleRate(sampleRate);
            }
        }
    }
}

void TrackManagerImpl::compileTimelineSnapshot() {
    if (!kernel_) return;

    pendingCompileSnapshot_.regionCount = 0;

    if (compilationScratch_.size() < 512) {
        compilationScratch_.resize(512);
    }

    for (auto& [trackIdVal, state] : tracks_) {
        TrackID tid = { trackIdVal, 1 };
        IPlaylist* playlist = getPlaylist(tid);
        if (!playlist) continue;

        uint32_t count = playlist->getAllRegions(compilationScratch_.data(), static_cast<uint32_t>(compilationScratch_.size()));
        while (count == compilationScratch_.size()) {
            compilationScratch_.resize(compilationScratch_.size() * 2);
            count = playlist->getAllRegions(compilationScratch_.data(), static_cast<uint32_t>(compilationScratch_.size()));
        }

        for (uint32_t i = 0; i < count; ++i) {
            if (pendingCompileSnapshot_.regionCount >= MAX_SNAPSHOT_REGIONS) {
                break;
            }

            const auto& rInfo = compilationScratch_[i];
            
            // Active Take Comping: Only layer 0 plays back
            if (rInfo.layer > 0) continue;
            
            SnapshotRegion& sRegion = pendingCompileSnapshot_.regions[pendingCompileSnapshot_.regionCount++];
            std::memset(&sRegion, 0, sizeof(SnapshotRegion));
            sRegion.trackId = { trackIdVal, 1 };
            sRegion.regionId = rInfo.id;
            sRegion.type = rInfo.region.type;
            sRegion.warpMode = rInfo.region.warpMode;
            sRegion.sourceId = rInfo.region.sourceId.id;
            sRegion.positionSample = rInfo.region.positionSample;
            sRegion.sourceStartSample = rInfo.region.sourceStartSample;
            sRegion.gain = rInfo.region.gain;
            sRegion.playbackRatio = rInfo.region.playbackRatio;
            sRegion.sourceBpm = rInfo.region.sourceBpm;
            sRegion.fadeInSamples = rInfo.region.fadeInSamples;
            sRegion.fadeOutSamples = rInfo.region.fadeOutSamples;
            uint32_t srcSampleRate = 44100;
            uint64_t durationProjectFrames = rInfo.region.sourceLength;
            if (rInfo.region.type == RegionType::AUDIO && sourceManager_) {
                composition::AudioSourceDescriptor desc;
                if (sourceManager_->getSource(rInfo.region.sourceId, desc)) {
                    srcSampleRate = desc.sampleRate;
                    if (desc.sampleRate > 0) {
                        double srRatio = static_cast<double>(projectSampleRate_) / static_cast<double>(desc.sampleRate);
                        double playbackRatio = rInfo.region.playbackRatio > 0.0f ? static_cast<double>(rInfo.region.playbackRatio) : 1.0;
                        durationProjectFrames = static_cast<uint64_t>(static_cast<double>(rInfo.region.sourceLength) * srRatio * playbackRatio);
                    }
                }
            }
            sRegion.durationProjectFrames = durationProjectFrames;
            sRegion.sourceSampleRate = srcSampleRate;
            sRegion.isMuted = rInfo.region.isMuted;
        }



        if (pendingCompileSnapshot_.regionCount >= MAX_SNAPSHOT_REGIONS) {
            break;
        }
    }

    // Sort snapshot regions primarily by trackId (to enable O(log N) binary search lookup in RT)
    // and secondarily by positionSample ascending.
    std::sort(pendingCompileSnapshot_.regions, 
              pendingCompileSnapshot_.regions + pendingCompileSnapshot_.regionCount,
              [](const SnapshotRegion& a, const SnapshotRegion& b) {
                  if (a.trackId.toRaw() != b.trackId.toRaw()) {
                      return a.trackId.toRaw() < b.trackId.toRaw();
                  }
                  return a.positionSample < b.positionSample;
              });

    kernel_->publishTimelineSnapshot(pendingCompileSnapshot_);
}

std::vector<ArrangementInfo> TrackManagerImpl::getArrangements() const {
    return arrangements_;
}

ArrangementID TrackManagerImpl::getActiveArrangement() const {
    return activeArrangementId_;
}

void TrackManagerImpl::setActiveArrangement(ArrangementID id) {
    if (!id.isValid()) return;
    
    bool found = false;
    for (auto& arr : arrangements_) {
        if (arr.id == id) {
            arr.isActive = true;
            found = true;
        } else {
            arr.isActive = false;
        }
    }
    
    if (found) {
        activeArrangementId_ = id;
        syncRTSequencerList();
        compileTimelineSnapshot();
    }
}

ArrangementID TrackManagerImpl::createArrangement(const char* name) {
    ArrangementID newId;
    newId.id = nextArrangementIdCounter_++;
    
    ArrangementInfo newArr;
    newArr.id = newId;
    std::strncpy(newArr.name, name, sizeof(newArr.name) - 1);
    newArr.name[sizeof(newArr.name) - 1] = '\0';
    newArr.isActive = false;
    newArr.padding[0] = 0;
    newArr.padding[1] = 0;
    newArr.padding[2] = 0;
    
    arrangements_.push_back(newArr);
    
    return newId;
}

void TrackManagerImpl::renameArrangement(ArrangementID id, const char* newName) {
    for (auto& arr : arrangements_) {
        if (arr.id == id) {
            std::strncpy(arr.name, newName, sizeof(arr.name) - 1);
            arr.name[sizeof(arr.name) - 1] = '\0';
            break;
        }
    }
}

void TrackManagerImpl::deleteArrangement(ArrangementID id) {
    if (!id.isValid()) return;
    
    // 1. Remove from arrangements_
    auto it = std::remove_if(arrangements_.begin(), arrangements_.end(), [id](const ArrangementInfo& arr) {
        return arr.id == id;
    });
    bool removed = (it != arrangements_.end());
    if (removed) {
        arrangements_.erase(it, arrangements_.end());
    }
    
    // 2. Clear state from all tracks
    for (auto& [trackIdVal, state] : tracks_) {
        state.arrangements.erase(id.id);
    }
    
    // 3. Fallback active
    if (activeArrangementId_ == id) {
        if (!arrangements_.empty()) {
            setActiveArrangement(arrangements_[0].id);
        } else {
            activeArrangementId_ = { 0xFFFFFFFFu };
            syncRTSequencerList();
            compileTimelineSnapshot();
        }
    }
}

ArrangementID TrackManagerImpl::cloneArrangement(ArrangementID id, const char* cloneName) {
    ArrangementID cloneId = createArrangement(cloneName);
    
    for (auto& [trackIdVal, trackState] : tracks_) {
        auto srcIt = trackState.arrangements.find(id.id);
        if (srcIt == trackState.arrangements.end()) continue;
        const auto& srcState = srcIt->second;
        
        auto& destState = trackState.arrangements[cloneId.id];
        TrackID tid = { trackIdVal, 1 };
        
        if (trackState.info.type == TrackType::AUDIO || trackState.info.type == TrackType::MIDI || 
            trackState.info.type == TrackType::INSTRUMENT) {
            destState.playlist = std::make_unique<PlaylistImpl>(tid, commandHistory_, sourceManager_);
        }
        if (trackState.info.type == TrackType::MIDI || trackState.info.type == TrackType::INSTRUMENT) {
            destState.midiSequencer = std::make_unique<MIDISequencerImpl>(tid, commandHistory_);
            destState.midiSequencer->setTargetNodeId(trackState.pipeline.sourceNode);
        }
        destState.automationManager = std::make_unique<AutomationLaneManagerImpl>(tid, commandHistory_);
        
        // Deep copy
        if (srcState.playlist && destState.playlist) {
            auto* srcPlayImpl = dynamic_cast<const PlaylistImpl*>(srcState.playlist.get());
            auto* destPlayImpl = dynamic_cast<PlaylistImpl*>(destState.playlist.get());
            if (srcPlayImpl && destPlayImpl) {
                destPlayImpl->copyFrom(srcPlayImpl);
            }
        }
        if (srcState.midiSequencer && destState.midiSequencer) {
            auto* srcSeqImpl = dynamic_cast<const MIDISequencerImpl*>(srcState.midiSequencer.get());
            auto* destSeqImpl = dynamic_cast<MIDISequencerImpl*>(destState.midiSequencer.get());
            if (srcSeqImpl && destSeqImpl) {
                destSeqImpl->copyFrom(srcSeqImpl);
            }
        }
        if (srcState.automationManager && destState.automationManager) {
            auto* srcAutoImpl = dynamic_cast<const AutomationLaneManagerImpl*>(srcState.automationManager.get());
            auto* destAutoImpl = dynamic_cast<AutomationLaneManagerImpl*>(destState.automationManager.get());
            if (srcAutoImpl && destAutoImpl) {
                destAutoImpl->copyFrom(srcAutoImpl);
            }
        }
    }
    
    return cloneId;
}

void TrackManagerImpl::mergeArrangements(
    ArrangementID sourceId,
    ArrangementID destId,
    int mergeMode,
    const MergeFilterOptions& filterOptions
) {
    if (!sourceId.isValid() || !destId.isValid()) return;
    
    bool sourceFound = false;
    bool destFound = false;
    for (const auto& arr : arrangements_) {
        if (arr.id == sourceId) sourceFound = true;
        if (arr.id == destId) destFound = true;
    }
    if (!sourceFound || !destFound) return;

    // 1. Calculate append offset if mergeMode == 1
    uint64_t destEndSample = 0;
    if (mergeMode == 1) {
        for (const auto& [trackIdVal, trackState] : tracks_) {
            auto it = trackState.arrangements.find(destId.id);
            if (it == trackState.arrangements.end()) continue;
            const auto& arrState = it->second;
            
            if (arrState.playlist) {
                std::vector<IPlaylist::RegionInfo> tempRegions(256);
                uint32_t count = arrState.playlist->getAllRegions(tempRegions.data(), static_cast<uint32_t>(tempRegions.size()));
                for (uint32_t i = 0; i < count; ++i) {
                    uint64_t endPos = tempRegions[i].region.positionSample + tempRegions[i].region.sourceLength;
                    if (endPos > destEndSample) {
                        destEndSample = endPos;
                    }
                }
            }
            
            if (arrState.midiSequencer) {
                if (auto* seqImpl = dynamic_cast<const MIDISequencerImpl*>(arrState.midiSequencer.get())) {
                    for (const auto& clip : seqImpl->getRawClipPositions()) {
                        uint64_t endPos = clip.positionSample + clip.sourceLength;
                        if (endPos > destEndSample) {
                            destEndSample = endPos;
                        }
                    }
                }
            }
        }
    }

    // 2. Perform merge based on mode
    if (mergeMode == 0 || mergeMode == 1) {
        for (auto& [trackIdVal, trackState] : tracks_) {
            auto srcIt = trackState.arrangements.find(sourceId.id);
            if (srcIt == trackState.arrangements.end()) continue;
            const auto& srcState = srcIt->second;
            
            TrackID tid = { trackIdVal, 1 };
            auto& destState = trackState.arrangements[destId.id];
            
            if (trackState.info.type == TrackType::AUDIO || trackState.info.type == TrackType::MIDI || 
                trackState.info.type == TrackType::INSTRUMENT) {
                if (!destState.playlist) {
                    destState.playlist = std::make_unique<PlaylistImpl>(tid, commandHistory_, sourceManager_);
                }
            }
            if (trackState.info.type == TrackType::MIDI || trackState.info.type == TrackType::INSTRUMENT) {
                if (!destState.midiSequencer) {
                    destState.midiSequencer = std::make_unique<MIDISequencerImpl>(tid, commandHistory_);
                    destState.midiSequencer->setTargetNodeId(trackState.pipeline.sourceNode);
                }
            }
            if (!destState.automationManager) {
                destState.automationManager = std::make_unique<AutomationLaneManagerImpl>(tid, commandHistory_);
            }
            
            if (srcState.playlist && destState.playlist) {
                std::vector<IPlaylist::RegionInfo> tempRegions(256);
                uint32_t count = srcState.playlist->getAllRegions(tempRegions.data(), static_cast<uint32_t>(tempRegions.size()));
                for (uint32_t i = 0; i < count; ++i) {
                    TimelineRegion region = tempRegions[i].region;

                    // FILTER 1: Audio filter
                    if (region.type == RegionType::AUDIO && !filterOptions.importAudio) {
                        continue;
                    }
                    // FILTER 2: MIDI filter
                    if (region.type == RegionType::MIDI && !filterOptions.importMIDI) {
                        continue;
                    }
                    // FILTER 3: Loop Range coordinate filter
                    if (filterOptions.limitToLoopRange) {
                        uint64_t regStart = region.positionSample;
                        uint64_t regEnd = regStart + region.sourceLength;
                        if (regStart < filterOptions.loopStartFrame || regEnd > filterOptions.loopEndFrame) {
                            continue;
                        }
                    }

                    region.positionSample += destEndSample;
                    destState.playlist->addRegion(region, tempRegions[i].layer);
                }
            }
            
            if (filterOptions.importMIDI && srcState.midiSequencer && destState.midiSequencer) {
                auto* srcSeqImpl = dynamic_cast<const MIDISequencerImpl*>(srcState.midiSequencer.get());
                auto* destSeqImpl = dynamic_cast<MIDISequencerImpl*>(destState.midiSequencer.get());
                if (srcSeqImpl && destSeqImpl) {
                    std::unordered_set<uint64_t> copiedClips;
                    for (const auto& clip : srcSeqImpl->getRawClipPositions()) {
                        if (filterOptions.limitToLoopRange) {
                            uint64_t clipStart = clip.positionSample;
                            uint64_t clipEnd = clipStart + clip.sourceLength;
                            if (clipStart < filterOptions.loopStartFrame || clipEnd > filterOptions.loopEndFrame) {
                                continue;
                            }
                        }
                        copiedClips.insert(clip.clipId.toRaw());
                        destSeqImpl->updateClipPosition(clip.clipId, clip.positionSample + destEndSample, clip.sourceLength);
                    }
                    for (const auto& entry : srcSeqImpl->getRawNotes()) {
                        if (copiedClips.count(entry.clipId.toRaw())) {
                            MIDINote shiftedNote = entry.note;
                            destSeqImpl->addNote(entry.clipId, shiftedNote);
                        }
                    }
                    for (const auto& cc : srcSeqImpl->getRawCCPoints()) {
                        if (copiedClips.count(cc.clipId.toRaw())) {
                            destSeqImpl->addCCPoint(cc.clipId, cc.point);
                        }
                    }
                    for (const auto& pb : srcSeqImpl->getRawPitchPoints()) {
                        if (copiedClips.count(pb.clipId.toRaw())) {
                            destSeqImpl->addPitchPoint(pb.clipId, pb.point);
                        }
                    }
                }
            }
            
            if (filterOptions.importAutomation && srcState.automationManager && destState.automationManager) {
                auto* srcAutoImpl = dynamic_cast<const AutomationLaneManagerImpl*>(srcState.automationManager.get());
                auto* destAutoImpl = dynamic_cast<AutomationLaneManagerImpl*>(destState.automationManager.get());
                if (srcAutoImpl && destAutoImpl) {
                    for (const auto& [target, lane] : srcAutoImpl->getLanes()) {
                        auto* srcLaneImpl = dynamic_cast<const AutomationLaneImpl*>(lane.get());
                        if (srcLaneImpl) {
                            IAutomationLane* destLane = destAutoImpl->createLane(target);
                            if (destLane) {
                                for (const auto& pt : srcLaneImpl->getPointsList()) {
                                    if (filterOptions.limitToLoopRange) {
                                        if (pt.positionSample < filterOptions.loopStartFrame || pt.positionSample > filterOptions.loopEndFrame) {
                                            continue;
                                        }
                                    }
                                    destLane->addPoint(pt.positionSample + destEndSample, pt.value, pt.curveShape, pt.tension);
                                }
                            }
                        }
                    }
                }
            }
        }
    } else if (mergeMode == 2) {
        std::vector<TrackID> tracksToDup;
        for (const auto& [trackIdVal, trackState] : tracks_) {
            if (trackState.arrangements.count(sourceId.id) > 0) {
                tracksToDup.push_back({ trackIdVal, 1 });
            }
        }
        
        for (TrackID srcTid : tracksToDup) {
            auto& srcTrackState = tracks_[srcTid.id];
            
            TrackCreateInfo newInfo = srcTrackState.info;
            newInfo.trackId = {0, 0};
            
            TrackID newTid = createTrackInternal(newInfo);
            auto& newTrackState = tracks_[newTid.id];
            
            auto srcArrIt = srcTrackState.arrangements.find(sourceId.id);
            if (srcArrIt != srcTrackState.arrangements.end()) {
                const auto& srcState = srcArrIt->second;
                
                auto& destState = newTrackState.arrangements[destId.id];
                if (newTrackState.info.type == TrackType::AUDIO || newTrackState.info.type == TrackType::MIDI || 
                    newTrackState.info.type == TrackType::INSTRUMENT) {
                    destState.playlist = std::make_unique<PlaylistImpl>(newTid, commandHistory_, sourceManager_);
                }
                if (newTrackState.info.type == TrackType::MIDI || newTrackState.info.type == TrackType::INSTRUMENT) {
                    destState.midiSequencer = std::make_unique<MIDISequencerImpl>(newTid, commandHistory_);
                    destState.midiSequencer->setTargetNodeId(newTrackState.pipeline.sourceNode);
                }
                destState.automationManager = std::make_unique<AutomationLaneManagerImpl>(newTid, commandHistory_);
                
                if (srcState.playlist && destState.playlist) {
                    auto* srcPlayImpl = dynamic_cast<const PlaylistImpl*>(srcState.playlist.get());
                    auto* destPlayImpl = dynamic_cast<PlaylistImpl*>(destState.playlist.get());
                    if (srcPlayImpl && destPlayImpl) {
                        std::vector<IPlaylist::RegionInfo> tempRegions(256);
                        uint32_t count = srcPlayImpl->getAllRegions(tempRegions.data(), static_cast<uint32_t>(tempRegions.size()));
                        for (uint32_t i = 0; i < count; ++i) {
                            TimelineRegion region = tempRegions[i].region;
                            if (region.type == RegionType::AUDIO && !filterOptions.importAudio) continue;
                            if (region.type == RegionType::MIDI && !filterOptions.importMIDI) continue;
                            if (filterOptions.limitToLoopRange) {
                                uint64_t regStart = region.positionSample;
                                uint64_t regEnd = regStart + region.sourceLength;
                                if (regStart < filterOptions.loopStartFrame || regEnd > filterOptions.loopEndFrame) {
                                    continue;
                                }
                            }
                            destPlayImpl->addRegion(region, tempRegions[i].layer);
                        }
                    }
                }
                if (filterOptions.importMIDI && srcState.midiSequencer && destState.midiSequencer) {
                    auto* srcSeqImpl = dynamic_cast<const MIDISequencerImpl*>(srcState.midiSequencer.get());
                    auto* destSeqImpl = dynamic_cast<MIDISequencerImpl*>(destState.midiSequencer.get());
                    if (srcSeqImpl && destSeqImpl) {
                        std::unordered_set<uint64_t> copiedClips;
                        for (const auto& clip : srcSeqImpl->getRawClipPositions()) {
                            if (filterOptions.limitToLoopRange) {
                                uint64_t clipStart = clip.positionSample;
                                uint64_t clipEnd = clipStart + clip.sourceLength;
                                if (clipStart < filterOptions.loopStartFrame || clipEnd > filterOptions.loopEndFrame) {
                                    continue;
                                }
                            }
                            copiedClips.insert(clip.clipId.toRaw());
                            destSeqImpl->updateClipPosition(clip.clipId, clip.positionSample, clip.sourceLength);
                        }
                        for (const auto& entry : srcSeqImpl->getRawNotes()) {
                            if (copiedClips.count(entry.clipId.toRaw())) {
                                MIDINote note = entry.note;
                                destSeqImpl->addNote(entry.clipId, note);
                            }
                        }
                        for (const auto& cc : srcSeqImpl->getRawCCPoints()) {
                            if (copiedClips.count(cc.clipId.toRaw())) {
                                destSeqImpl->addCCPoint(cc.clipId, cc.point);
                            }
                        }
                        for (const auto& pb : srcSeqImpl->getRawPitchPoints()) {
                            if (copiedClips.count(pb.clipId.toRaw())) {
                                destSeqImpl->addPitchPoint(pb.clipId, pb.point);
                            }
                        }
                    }
                }
                if (filterOptions.importAutomation && srcState.automationManager && destState.automationManager) {
                    auto* srcAutoImpl = dynamic_cast<const AutomationLaneManagerImpl*>(srcState.automationManager.get());
                    auto* destAutoImpl = dynamic_cast<AutomationLaneManagerImpl*>(destState.automationManager.get());
                    if (srcAutoImpl && destAutoImpl) {
                        for (const auto& [target, lane] : srcAutoImpl->getLanes()) {
                            auto* srcLaneImpl = dynamic_cast<const AutomationLaneImpl*>(lane.get());
                            if (srcLaneImpl) {
                                IAutomationLane* destLane = destAutoImpl->createLane(target);
                                if (destLane) {
                                    for (const auto& pt : srcLaneImpl->getPointsList()) {
                                        if (filterOptions.limitToLoopRange) {
                                            if (pt.positionSample < filterOptions.loopStartFrame || pt.positionSample > filterOptions.loopEndFrame) {
                                                continue;
                                            }
                                        }
                                        destLane->addPoint(pt.positionSample, pt.value, pt.curveShape, pt.tension);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    if (activeArrangementId_ == destId) {
        syncRTSequencerList();
        compileTimelineSnapshot();
    }
}

void TrackManagerImpl::recalculateTimeCaches(Layer2::ITempoService* tempoService) {
    if (!tempoService) return;

    uint32_t arrId = activeArrangementId_.isValid() ? activeArrangementId_.id : 1;

    for (auto& [trackIdVal, state] : tracks_) {
        auto arrIt = state.arrangements.find(arrId);
        if (arrIt == state.arrangements.end()) continue;

        auto& arrState = arrIt->second;

        // --- Recalculate Playlist region positionSamples ---
        if (arrState.playlist) {
            if (auto* playImpl = dynamic_cast<PlaylistImpl*>(arrState.playlist.get())) {
                for (auto& entry : playImpl->getMutableRegions()) {
                    const MusicalPosition& mp = entry.region.startPosition;
                    // Only recalculate if the musical anchor has been set (bar >= 1)
                    if (mp.bar >= 1) {
                        Layer2::BBTPosition bbt(mp.bar, mp.beat, mp.tick);
                        entry.region.positionSample = tempoService->bbtToSamples(bbt);
                    }
                }
            }
        }

        // --- Recalculate MIDI sequencer clip positions and note sample caches ---
        if (arrState.midiSequencer) {
            if (auto* seqImpl = dynamic_cast<MIDISequencerImpl*>(arrState.midiSequencer.get())) {
                seqImpl->recalculateTimeCaches(tempoService);
            }
        }
    }

    // Republish the new data to the real-time thread
    syncRTSequencerList();
    compileTimelineSnapshot();
}

void TrackManagerImpl::registerMixerRoutingCallback(MixerRoutingCallback cb) {
    mixerRoutingCallback_ = cb;
}

void TrackManagerImpl::setTrackFaderGain(TrackID id, float gainLinear) {
    float oldVal = getTrackFaderGain(id);
    setTrackFaderGainInternal(id, gainLinear);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::SET_FADER_GAIN;
        delta.targetId = handleToUint64(id);
        MixerParamPayload p{id, 0, oldVal, gainLinear};
        delta.oldStateSize = sizeof(MixerParamPayload);
        std::memcpy(delta.oldState, &p, sizeof(MixerParamPayload));
        delta.newStateSize = sizeof(MixerParamPayload);
        std::memcpy(delta.newState, &p, sizeof(MixerParamPayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackFaderGainInternal(TrackID id, float gainLinear) {
    NodeID targetNode = NodeID::invalid();
    if (id.id == 0 && id.generation == 0) {
        targetNode = masterChannelStripNode_;
    } else {
        auto it = tracks_.find(id.id);
        if (it != tracks_.end()) {
            it->second.faderGain = gainLinear;
            targetNode = it->second.pipeline.trackNode;
        }
    }
    
    if (targetNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(targetNode)) {
            trk->channelStrip.targetGain.store(gainLinear, std::memory_order_release);
            trk->channelStrip.gainSmoother.setTarget(gainLinear);
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(targetNode)) {
            trkInst->channelStrip.targetGain.store(gainLinear, std::memory_order_release);
            trkInst->channelStrip.gainSmoother.setTarget(gainLinear);
        } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(targetNode)) {
            cs->targetGain.store(gainLinear, std::memory_order_release);
            cs->gainSmoother.setTarget(gainLinear);
        }
        SystemMutation mut{};
        mut.type = 30; // MutationType::PARAMETER_SET
        mut.targetId = targetNode;
        mut.priority = 128;
        mut.payload[0] = static_cast<uint32_t>(TrackMacroParameter::Volume);
        std::memcpy(&mut.payload[1], &gainLinear, sizeof(float));
        if (mutationBridge_) mutationBridge_->pushMutation(mut);
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

float TrackManagerImpl::getTrackFaderGain(TrackID id) const {
    if (id.id == 0 && id.generation == 0) {
        if (masterChannelStripNode_.isValid()) {
            if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(masterChannelStripNode_)) {
                return cs->targetGain.load(std::memory_order_acquire);
            }
        }
        return 1.0f;
    }
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) return it->second.faderGain;
    return 1.0f;
}

void TrackManagerImpl::setTrackPan(TrackID id, float panPosition) {
    float oldVal = getTrackPan(id);
    setTrackPanInternal(id, panPosition);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::SET_PAN;
        delta.targetId = handleToUint64(id);
        MixerParamPayload p{id, 1, oldVal, panPosition};
        delta.oldStateSize = sizeof(MixerParamPayload);
        std::memcpy(delta.oldState, &p, sizeof(MixerParamPayload));
        delta.newStateSize = sizeof(MixerParamPayload);
        std::memcpy(delta.newState, &p, sizeof(MixerParamPayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackPanInternal(TrackID id, float panPosition) {
    NodeID channelStripNode = NodeID::invalid();
    float clampedPan = std::clamp(panPosition, 0.0f, 1.0f);
    
    if (id.id == 0 && id.generation == 0) {
        channelStripNode = masterChannelStripNode_;
    } else {
        auto it = tracks_.find(id.id);
        if (it != tracks_.end()) {
            it->second.pan = clampedPan;
            channelStripNode = it->second.pipeline.trackNode;
        }
    }
    
    if (channelStripNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(channelStripNode)) {
            trk->channelStrip.targetPan.store(clampedPan, std::memory_order_release);
            trk->channelStrip.panSmoother.setTarget(clampedPan);
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(channelStripNode)) {
            trkInst->channelStrip.targetPan.store(clampedPan, std::memory_order_release);
            trkInst->channelStrip.panSmoother.setTarget(clampedPan);
        } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(channelStripNode)) {
            cs->targetPan.store(clampedPan, std::memory_order_release);
            cs->panSmoother.setTarget(clampedPan);
        }
        SystemMutation mut{};
        mut.type = 30;
        mut.targetId = channelStripNode;
        mut.priority = 128;
        mut.payload[0] = static_cast<uint32_t>(TrackMacroParameter::Pan);
        std::memcpy(&mut.payload[1], &clampedPan, sizeof(float));
        if (mutationBridge_) mutationBridge_->pushMutation(mut);
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

float TrackManagerImpl::getTrackPan(TrackID id) const {
    if (id.id == 0 && id.generation == 0) {
        if (masterChannelStripNode_.isValid()) {
            if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(masterChannelStripNode_)) {
                return cs->targetPan.load(std::memory_order_acquire);
            }
        }
        return 0.5f;
    }
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) return it->second.pan;
    return 0.5f;
}

void TrackManagerImpl::setTrackMute(TrackID id, bool mute) {
    bool oldMute = getTrackMute(id);
    setTrackMuteInternal(id, mute);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::SET_MUTE;
        delta.targetId = handleToUint64(id);
        MixerParamPayload p{id, 2, oldMute ? 1.0f : 0.0f, mute ? 1.0f : 0.0f};
        delta.oldStateSize = sizeof(MixerParamPayload);
        std::memcpy(delta.oldState, &p, sizeof(MixerParamPayload));
        delta.newStateSize = sizeof(MixerParamPayload);
        std::memcpy(delta.newState, &p, sizeof(MixerParamPayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackMuteInternal(TrackID id, bool mute) {
    NodeID channelStripNode = NodeID::invalid();
    if (id.id == 0 && id.generation == 0) {
        channelStripNode = masterChannelStripNode_;
    } else {
        auto it = tracks_.find(id.id);
        if (it != tracks_.end()) {
            it->second.mute = mute;
            channelStripNode = it->second.pipeline.trackNode;
        }
    }
    
    if (channelStripNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(channelStripNode)) {
            trk->channelStrip.mute.store(mute, std::memory_order_release);
            trk->channelStrip.muteRamp.setTarget(mute ? 0.0f : 1.0f);
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(channelStripNode)) {
            trkInst->channelStrip.mute.store(mute, std::memory_order_release);
            trkInst->channelStrip.muteRamp.setTarget(mute ? 0.0f : 1.0f);
        } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(channelStripNode)) {
            cs->mute.store(mute, std::memory_order_release);
            cs->muteRamp.setTarget(mute ? 0.0f : 1.0f);
        }
        SystemMutation mut{};
        mut.type = 30;
        mut.targetId = channelStripNode;
        mut.priority = 128;
        mut.payload[0] = static_cast<uint32_t>(TrackMacroParameter::Mute);
        float val = mute ? 1.0f : 0.0f;
        std::memcpy(&mut.payload[1], &val, sizeof(float));
        if (mutationBridge_) mutationBridge_->pushMutation(mut);
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

bool TrackManagerImpl::getTrackMute(TrackID id) const {
    if (id.id == 0 && id.generation == 0) {
        if (masterChannelStripNode_.isValid()) {
            if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(masterChannelStripNode_)) {
                return cs->mute.load(std::memory_order_acquire);
            }
        }
        return false;
    }
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) return it->second.mute;
    return false;
}

void TrackManagerImpl::setTrackSolo(TrackID id, bool solo) {
    bool oldSolo = getTrackSolo(id);
    setTrackSoloInternal(id, solo);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::SET_SOLO;
        delta.targetId = handleToUint64(id);
        MixerParamPayload p{id, 3, oldSolo ? 1.0f : 0.0f, solo ? 1.0f : 0.0f};
        delta.oldStateSize = sizeof(MixerParamPayload);
        std::memcpy(delta.oldState, &p, sizeof(MixerParamPayload));
        delta.newStateSize = sizeof(MixerParamPayload);
        std::memcpy(delta.newState, &p, sizeof(MixerParamPayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackSoloInternal(TrackID id, bool solo) {
    if (id.id == 0 && id.generation == 0) {
        if (masterChannelStripNode_.isValid()) {
            if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(masterChannelStripNode_)) {
                cs->solo.store(solo, std::memory_order_release);
                cs->soloRamp.setTarget(solo ? 1.0f : 0.0f);
            }
            SystemMutation mut{};
            mut.type = 30;
            mut.targetId = masterChannelStripNode_;
            mut.priority = 128;
            mut.payload[0] = 3;
            float val = solo ? 1.0f : 0.0f;
            std::memcpy(&mut.payload[1], &val, sizeof(float));
            if (mutationBridge_) mutationBridge_->pushMutation(mut);
        }
        if (mixerRoutingCallback_) mixerRoutingCallback_(id);
        return;
    }
    
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;
    it->second.solo = solo;
    
    auto desc = it->second.pipeline;
    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            trk->channelStrip.solo.store(solo, std::memory_order_release);
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            trkInst->channelStrip.solo.store(solo, std::memory_order_release);
        } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(desc.trackNode)) {
            cs->solo.store(solo, std::memory_order_release);
        }
    }
    
    bool anySoloed = false;
    for (auto& [tid, state] : tracks_) {
        if (state.pipeline.trackNode.isValid()) {
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(state.pipeline.trackNode)) {
                if (trk->channelStrip.solo.load(std::memory_order_acquire)) { anySoloed = true; break; }
            } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(state.pipeline.trackNode)) {
                if (trkInst->channelStrip.solo.load(std::memory_order_acquire)) { anySoloed = true; break; }
            } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(state.pipeline.trackNode)) {
                if (cs->solo.load(std::memory_order_acquire)) { anySoloed = true; break; }
            }
        }
    }
    
    for (auto& [tid, state] : tracks_) {
        if (state.pipeline.trackNode.isValid()) {
            float val = 1.0f;
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(state.pipeline.trackNode)) {
                if (anySoloed) val = trk->channelStrip.solo.load(std::memory_order_acquire) ? 1.0f : 0.0f;
                trk->channelStrip.soloRamp.setTarget(val);
            } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(state.pipeline.trackNode)) {
                if (anySoloed) val = trkInst->channelStrip.solo.load(std::memory_order_acquire) ? 1.0f : 0.0f;
                trkInst->channelStrip.soloRamp.setTarget(val);
            } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(state.pipeline.trackNode)) {
                if (anySoloed) val = cs->solo.load(std::memory_order_acquire) ? 1.0f : 0.0f;
                cs->soloRamp.setTarget(val);
            }
            
            SystemMutation mut{};
            mut.type = 30;
            mut.targetId = state.pipeline.trackNode;
            mut.priority = 128;
            mut.payload[0] = static_cast<uint32_t>(TrackMacroParameter::Solo);
            std::memcpy(&mut.payload[1], &val, sizeof(float));
            if (mutationBridge_) mutationBridge_->pushMutation(mut);
        }
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

bool TrackManagerImpl::getTrackSolo(TrackID id) const {
    if (id.id == 0 && id.generation == 0) {
        if (masterChannelStripNode_.isValid()) {
            if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(masterChannelStripNode_)) {
                return cs->solo.load(std::memory_order_acquire);
            }
        }
        return false;
    }
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) return it->second.solo;
    return false;
}

void TrackManagerImpl::setTrackSendGain(TrackID id, bool isPreFader, uint32_t sendIndex, float gainLinear) {
    if (sendIndex >= 4) return;
    float oldVal = getTrackSendGain(id, isPreFader, sendIndex);
    setTrackSendGainInternal(id, isPreFader, sendIndex, gainLinear);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::SET_SEND_GAIN;
        delta.targetId = handleToUint64(id);
        SendRoutingPayload p{id, sendIndex, isPreFader, 0, oldVal, gainLinear, NodeID::invalid(), NodeID::invalid()};
        delta.oldStateSize = sizeof(SendRoutingPayload);
        std::memcpy(delta.oldState, &p, sizeof(SendRoutingPayload));
        delta.newStateSize = sizeof(SendRoutingPayload);
        std::memcpy(delta.newState, &p, sizeof(SendRoutingPayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackSendGainInternal(TrackID id, bool isPreFader, uint32_t sendIndex, float gainLinear) {
    if (sendIndex >= MAX_TRACK_SENDS) return;
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;
    
    auto& send = isPreFader ? it->second.preSends[sendIndex] : it->second.postSends[sendIndex];
    send.gainLinear = gainLinear;
    
    auto desc = it->second.pipeline;
    if (desc.trackNode.isValid()) {
        float targetGain = send.isEnabled ? gainLinear : 0.0f;
        uint32_t sendParamIdx = static_cast<uint32_t>(TrackMacroParameter::Send0Gain) + (sendIndex * 3);
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            trk->sends[sendIndex].targetGain.store(targetGain, std::memory_order_release);
            trk->sends[sendIndex].gainSmoother.setTarget(targetGain);
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            trkInst->sends[sendIndex].targetGain.store(targetGain, std::memory_order_release);
            trkInst->sends[sendIndex].gainSmoother.setTarget(targetGain);
        }
        SystemMutation mut{};
        mut.type = 30;
        mut.targetId = desc.trackNode;
        mut.priority = 128;
        mut.payload[0] = sendParamIdx;
        std::memcpy(&mut.payload[1], &targetGain, sizeof(float));
        if (mutationBridge_) mutationBridge_->pushMutation(mut);
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

float TrackManagerImpl::getTrackSendGain(TrackID id, bool isPreFader, uint32_t sendIndex) const {
    if (sendIndex >= MAX_TRACK_SENDS) return 0.0f;
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        const auto& send = isPreFader ? it->second.preSends[sendIndex] : it->second.postSends[sendIndex];
        return send.gainLinear;
    }
    return 0.0f;
}

void TrackManagerImpl::setTrackSendPan(TrackID id, bool isPreFader, uint32_t sendIndex, float panPosition) {
    if (sendIndex >= MAX_TRACK_SENDS) return;
    float oldVal = getTrackSendPan(id, isPreFader, sendIndex);
    setTrackSendPanInternal(id, isPreFader, sendIndex, panPosition);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::SET_SEND_PAN;
        delta.targetId = handleToUint64(id);
        SendRoutingPayload p{id, sendIndex, isPreFader, 1, oldVal, panPosition, NodeID::invalid(), NodeID::invalid()};
        delta.oldStateSize = sizeof(SendRoutingPayload);
        std::memcpy(delta.oldState, &p, sizeof(SendRoutingPayload));
        delta.newStateSize = sizeof(SendRoutingPayload);
        std::memcpy(delta.newState, &p, sizeof(SendRoutingPayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackSendPanInternal(TrackID id, bool isPreFader, uint32_t sendIndex, float panPosition) {
    if (sendIndex >= MAX_TRACK_SENDS) return;
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;
    
    auto& send = isPreFader ? it->second.preSends[sendIndex] : it->second.postSends[sendIndex];
    send.panPosition = panPosition;
    
    auto desc = it->second.pipeline;
    if (desc.trackNode.isValid()) {
        uint32_t panParamIdx = static_cast<uint32_t>(TrackMacroParameter::Send0Pan) + (sendIndex * 3);
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            trk->sends[sendIndex].targetPan.store(panPosition, std::memory_order_release);
            trk->sends[sendIndex].panSmoother.setTarget(panPosition);
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            trkInst->sends[sendIndex].targetPan.store(panPosition, std::memory_order_release);
            trkInst->sends[sendIndex].panSmoother.setTarget(panPosition);
        }
        SystemMutation mut{};
        mut.type = 30;
        mut.targetId = desc.trackNode;
        mut.priority = 128;
        mut.payload[0] = panParamIdx;
        std::memcpy(&mut.payload[1], &panPosition, sizeof(float));
        if (mutationBridge_) mutationBridge_->pushMutation(mut);
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

float TrackManagerImpl::getTrackSendPan(TrackID id, bool isPreFader, uint32_t sendIndex) const {
    if (sendIndex >= MAX_TRACK_SENDS) return 0.5f;
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        const auto& send = isPreFader ? it->second.preSends[sendIndex] : it->second.postSends[sendIndex];
        return send.panPosition;
    }
    return 0.5f;
}

void TrackManagerImpl::setTrackSendEnabled(TrackID id, bool isPreFader, uint32_t sendIndex, bool enabled) {
    if (sendIndex >= MAX_TRACK_SENDS) return;
    bool oldVal = getTrackSendEnabled(id, isPreFader, sendIndex);
    setTrackSendEnabledInternal(id, isPreFader, sendIndex, enabled);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::SET_SEND_ENABLED;
        delta.targetId = handleToUint64(id);
        SendRoutingPayload p{id, sendIndex, isPreFader, 2, oldVal ? 1.0f : 0.0f, enabled ? 1.0f : 0.0f, NodeID::invalid(), NodeID::invalid()};
        delta.oldStateSize = sizeof(SendRoutingPayload);
        std::memcpy(delta.oldState, &p, sizeof(SendRoutingPayload));
        delta.newStateSize = sizeof(SendRoutingPayload);
        std::memcpy(delta.newState, &p, sizeof(SendRoutingPayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackSendEnabledInternal(TrackID id, bool isPreFader, uint32_t sendIndex, bool enabled) {
    if (sendIndex >= MAX_TRACK_SENDS) return;
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;
    
    auto& send = isPreFader ? it->second.preSends[sendIndex] : it->second.postSends[sendIndex];
    send.isEnabled = enabled;
    
    auto desc = it->second.pipeline;
    if (desc.trackNode.isValid()) {
        float targetGain = enabled ? send.gainLinear : 0.0f;
        uint32_t sendParamIdx = static_cast<uint32_t>(TrackMacroParameter::Send0Gain) + (sendIndex * 3);
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            trk->sends[sendIndex].targetGain.store(targetGain, std::memory_order_release);
            trk->sends[sendIndex].gainSmoother.setTarget(targetGain);
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            trkInst->sends[sendIndex].targetGain.store(targetGain, std::memory_order_release);
            trkInst->sends[sendIndex].gainSmoother.setTarget(targetGain);
        }
        SystemMutation mut{};
        mut.type = 30;
        mut.targetId = desc.trackNode;
        mut.priority = 128;
        mut.payload[0] = sendParamIdx;
        std::memcpy(&mut.payload[1], &targetGain, sizeof(float));
        if (mutationBridge_) mutationBridge_->pushMutation(mut);
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

bool TrackManagerImpl::getTrackSendEnabled(TrackID id, bool isPreFader, uint32_t sendIndex) const {
    if (sendIndex >= 4) return false;
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        const auto& send = isPreFader ? it->second.preSends[sendIndex] : it->second.postSends[sendIndex];
        return send.isEnabled;
    }
    return false;
}

void TrackManagerImpl::setTrackSendDestination(TrackID id, bool isPreFader, uint32_t sendIndex, NodeID destinationNodeId) {
    if (sendIndex >= 4) return;
    NodeID oldVal = getTrackSendDestination(id, isPreFader, sendIndex);
    setTrackSendDestinationInternal(id, isPreFader, sendIndex, destinationNodeId);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::SET_SEND_DEST;
        delta.targetId = handleToUint64(id);
        SendRoutingPayload p{id, sendIndex, isPreFader, 3, 0.0f, 0.0f, oldVal, destinationNodeId};
        delta.oldStateSize = sizeof(SendRoutingPayload);
        std::memcpy(delta.oldState, &p, sizeof(SendRoutingPayload));
        delta.newStateSize = sizeof(SendRoutingPayload);
        std::memcpy(delta.newState, &p, sizeof(SendRoutingPayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackSendDestinationInternal(TrackID id, bool isPreFader, uint32_t sendIndex, NodeID destinationNodeId) {
    if (sendIndex >= 4) return;
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;
    
    auto& send = isPreFader ? it->second.preSends[sendIndex] : it->second.postSends[sendIndex];
    NodeID oldDest = send.destinationNodeId;
    send.destinationNodeId = destinationNodeId;
    
    // Resolve name
    std::string destName = "-- Empty --";
    if (destinationNodeId.isValid()) {
        if (destinationNodeId == masterChannelStripNode_) {
            destName = "MASTER";
        } else {
            for (auto& [tid, state] : tracks_) {
                if (state.pipeline.trackNode == destinationNodeId) {
                    destName = "Track " + std::to_string(tid);
                    break;
                }
            }
        }
    }
    send.destinationName = destName;
    
    if (mutationBridge_) {
        auto desc = it->second.pipeline;
        if (desc.trackNode.isValid()) {
            uint32_t trackNodeType = (it->second.info.type == composition::TrackType::INSTRUMENT) ? 
                DSP::NODE_TYPE_INSTRUMENT_TRACK : DSP::NODE_TYPE_AUDIO_TRACK;
            uint16_t sendPortLeft = static_cast<uint16_t>(TRACK_MAIN_OUTPUT_CHANNELS + (sendIndex * 2));
            uint16_t sendPortRight = static_cast<uint16_t>(sendPortLeft + 1);

            if (oldDest.isValid()) {
                uint32_t destNodeType = (oldDest == masterChannelStripNode_) ? DSP::NODE_TYPE_BUS : DSP::NODE_TYPE_AUDIO_TRACK;
                uint32_t oldPortBase = (oldDest == masterChannelStripNode_) ? 0 : TRACK_INPUT_PLAYBACK_PORT_BASE;
                // Disconnect left & right send channels
                SystemMutation discL{};
                discL.type = 13; // NODE_DISCONNECT
                discL.priority = 128;
                discL.connection.sourceNodeIndex = (trackNodeType << 16) | (desc.trackNode.id & 0xFFFF);
                discL.connection.sourcePort = sendPortLeft;
                discL.connection.destNodeIndex = (destNodeType << 16) | (oldDest.id & 0xFFFF);
                discL.connection.destPort = oldPortBase + 0;
                discL.connection.gain = 1.0f;
                mutationBridge_->pushMutation(discL);

                SystemMutation discR{};
                discR.type = 13;
                discR.priority = 128;
                discR.connection.sourceNodeIndex = (trackNodeType << 16) | (desc.trackNode.id & 0xFFFF);
                discR.connection.sourcePort = sendPortRight;
                discR.connection.destNodeIndex = (destNodeType << 16) | (oldDest.id & 0xFFFF);
                discR.connection.destPort = oldPortBase + 1;
                discR.connection.gain = 1.0f;
                mutationBridge_->pushMutation(discR);
            }
            
            if (destinationNodeId.isValid()) {
                uint32_t destNodeType = (destinationNodeId == masterChannelStripNode_) ? DSP::NODE_TYPE_BUS : DSP::NODE_TYPE_AUDIO_TRACK;
                uint32_t newPortBase = (destinationNodeId == masterChannelStripNode_) ? 0 : TRACK_INPUT_PLAYBACK_PORT_BASE;
                // Connect left & right send channels
                SystemMutation connL{};
                connL.type = 12; // NODE_CONNECT
                connL.priority = 128;
                connL.connection.sourceNodeIndex = (trackNodeType << 16) | (desc.trackNode.id & 0xFFFF);
                connL.connection.sourcePort = sendPortLeft;
                connL.connection.destNodeIndex = (destNodeType << 16) | (destinationNodeId.id & 0xFFFF);
                connL.connection.destPort = newPortBase + 0;
                connL.connection.gain = 1.0f;
                mutationBridge_->pushMutation(connL);

                SystemMutation connR{};
                connR.type = 12;
                connR.priority = 128;
                connR.connection.sourceNodeIndex = (trackNodeType << 16) | (desc.trackNode.id & 0xFFFF);
                connR.connection.sourcePort = sendPortRight;
                connR.connection.destNodeIndex = (destNodeType << 16) | (destinationNodeId.id & 0xFFFF);
                connR.connection.destPort = newPortBase + 1;
                connR.connection.gain = 1.0f;
                mutationBridge_->pushMutation(connR);
            }
        }
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

NodeID TrackManagerImpl::getTrackSendDestination(TrackID id, bool isPreFader, uint32_t sendIndex) const {
    if (sendIndex >= 4) return NodeID::invalid();
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        const auto& send = isPreFader ? it->second.preSends[sendIndex] : it->second.postSends[sendIndex];
        return send.destinationNodeId;
    }
    return NodeID::invalid();
}

std::string TrackManagerImpl::getTrackSendDestinationName(TrackID id, bool isPreFader, uint32_t sendIndex) const {
    if (sendIndex >= 4) return "-- Empty --";
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        const auto& send = isPreFader ? it->second.preSends[sendIndex] : it->second.postSends[sendIndex];
        return send.destinationName;
    }
    return "-- Empty --";
}

void TrackManagerImpl::setTrackAudioInputChannel(TrackID id, uint32_t mappedPhysicalInputIndex, uint32_t numChannels) {
    setTrackAudioInputChannelInternal(id, mappedPhysicalInputIndex, numChannels);
}

void TrackManagerImpl::setTrackAudioInputChannelInternal(TrackID id, uint32_t mappedPhysicalInputIndex, uint32_t numChannels) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;
    
    it->second.physicalInputIndex = mappedPhysicalInputIndex;
    it->second.inputChannelCount = numChannels;
    it->second.info.inputSourceIndex = mappedPhysicalInputIndex;
    
    auto desc = it->second.pipeline;
    if (desc.audioInputNode.isValid()) {
        if (auto* nodeState = DSP::AudioInputFactory::getRegistry().get(desc.audioInputNode)) {
            nodeState->buffers[0].hardwareChannelIndex = static_cast<uint8_t>(mappedPhysicalInputIndex);
            nodeState->buffers[0].numChannels = static_cast<uint8_t>(numChannels);
            nodeState->buffers[1].hardwareChannelIndex = static_cast<uint8_t>(mappedPhysicalInputIndex);
            nodeState->buffers[1].numChannels = static_cast<uint8_t>(numChannels);
            
            SystemMutation mut{};
            mut.type = 31; // NODE_STATE_CHANGE
            mut.targetId = desc.audioInputNode;
            mut.priority = 128;
            mut.payload[0] = 0;
            if (mutationBridge_) mutationBridge_->pushMutation(mut);
        }
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

void TrackManagerImpl::insertTrackPlugin(TrackID id, uint32_t slotIndex, uint32_t pluginId) {
    if (slotIndex >= 8) return;
    
    bool isMaster = (id.id == 0 && id.generation == 0);
    if (!isMaster) {
        auto it = tracks_.find(id.id);
        if (it == tracks_.end()) return;
    }

    insertTrackPluginInternal(id, slotIndex, pluginId, 0);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::INSERT_PLUGIN;
        delta.targetId = handleToUint64(id);
        PluginLifecyclePayload p{id, slotIndex, pluginId, MixerRoutingOps::INSERT_PLUGIN, 0, false, false};
        delta.oldStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.oldState, &p, sizeof(PluginLifecyclePayload));
        delta.newStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.newState, &p, sizeof(PluginLifecyclePayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::insertTrackPluginInternal(TrackID id, uint32_t slotIndex, uint32_t pluginId, uint32_t stateId) {
    if (slotIndex >= 8) return;
    
    bool isMaster = (id.id == 0 && id.generation == 0);
    NodeID insertPluginChainHead = NodeID::invalid();
    PluginState* pluginsArray = nullptr;
    DSP::PluginSlotState* slotNode = nullptr;
    
    if (isMaster) {
        insertPluginChainHead = masterPluginSlotNode_;
        pluginsArray = masterPlugins_;
        slotNode = DSP::PluginSlotFactory::getRegistry().get(masterPluginSlotNode_);
    } else {
        auto it = tracks_.find(id.id);
        if (it == tracks_.end()) return;
        insertPluginChainHead = it->second.pipeline.trackNode;
        pluginsArray = it->second.plugins;
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(insertPluginChainHead)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(insertPluginChainHead)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    
    if (!slotNode) return;
    
    PluginDescriptor plugDesc{};
    bool found = false;
    if (pluginManager_) {
        auto available = pluginManager_->getAvailablePlugins();
        for (const auto& p : available) {
            if (p.pluginId == pluginId) {
                plugDesc = p;
                found = true;
                break;
            }
        }
    }
    if (!found) return;
    
    auto optId = DSP::InsertPluginFactory::getRegistry().allocate();
    if (optId) {
        NodeID pluginNodeId = *optId;
        if (pluginNodeId.isValid()) {
            if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(pluginNodeId)) {
                pluginState->reset();
                auto instance = pluginManager_->instantiatePlugin(plugDesc);
                if (instance) {
                    pluginState->pluginInstance = instance.release();
                    
                    if (stateId != 0) {
                        const auto* stateBuffer = pluginStateCache_.getState(stateId);
                        if (stateBuffer) {
                            static_cast<Layer3::IPlugin*>(pluginState->pluginInstance)->loadState(stateBuffer->data(), stateBuffer->size());
                        }
                    }
                }
                
                PluginFormat format = PluginFormat::NONE;
                if (plugDesc.formatFlags & PluginFormatFlags::VST3) format = PluginFormat::VST3;
                else if (plugDesc.formatFlags & PluginFormatFlags::CLAP) format = PluginFormat::CLAP;
                else if (plugDesc.formatFlags & PluginFormatFlags::AU) format = PluginFormat::AU;
                
                pluginState->pluginHandle = { plugDesc.pluginId, 1, format };
                std::strncpy(pluginState->name, plugDesc.name, sizeof(pluginState->name) - 1);
                pluginState->name[sizeof(pluginState->name) - 1] = '\0';
            }
            
            std::atomic_thread_fence(std::memory_order_release);
            
            slotNode->slots[slotIndex] = pluginNodeId;
            slotNode->bypass[slotIndex] = false;
            
            if (pluginsArray) {
                auto& plugState = pluginsArray[slotIndex];
                plugState.pluginId = pluginId;
                plugState.bypassed = false;
                if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(pluginNodeId)) {
                    plugState.pluginInstance = pluginState->pluginInstance;
                    std::strncpy(plugState.name, pluginState->name, sizeof(plugState.name) - 1);
                    plugState.name[sizeof(plugState.name) - 1] = '\0';
                }
            }
        }
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

void TrackManagerImpl::removeTrackPlugin(TrackID id, uint32_t slotIndex) {
    if (slotIndex >= 8) return;
    
    uint32_t stateId = 0;
    uint32_t pluginId = 0;
    bool isMaster = (id.id == 0 && id.generation == 0);
    
    if (isMaster) {
        pluginId = masterPlugins_[slotIndex].pluginId;
        if (masterPlugins_[slotIndex].pluginInstance) {
            auto* plugin = static_cast<Layer3::IPlugin*>(masterPlugins_[slotIndex].pluginInstance);
            std::vector<uint8_t> buffer = plugin->getState();
            if (!buffer.empty()) {
                stateId = pluginStateCache_.storeState(std::move(buffer));
            }
        }
    } else {
        auto it = tracks_.find(id.id);
        if (it != tracks_.end()) {
            pluginId = it->second.plugins[slotIndex].pluginId;
            if (it->second.plugins[slotIndex].pluginInstance) {
                auto* plugin = static_cast<Layer3::IPlugin*>(it->second.plugins[slotIndex].pluginInstance);
                std::vector<uint8_t> buffer = plugin->getState();
                if (!buffer.empty()) {
                    stateId = pluginStateCache_.storeState(std::move(buffer));
                }
            }
        } else {
            return;
        }
    }
    
    removeTrackPluginInternal(id, slotIndex);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::REMOVE_PLUGIN;
        delta.targetId = handleToUint64(id);
        PluginLifecyclePayload p{id, slotIndex, pluginId, MixerRoutingOps::REMOVE_PLUGIN, stateId, false, false};
        delta.oldStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.oldState, &p, sizeof(PluginLifecyclePayload));
        delta.newStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.newState, &p, sizeof(PluginLifecyclePayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::removeTrackPluginInternal(TrackID id, uint32_t slotIndex) {
    if (slotIndex >= 8) return;
    
    clearTrackSidechainRouting(id, slotIndex);
    
    bool isMaster = (id.id == 0 && id.generation == 0);
    NodeID insertPluginChainHead = NodeID::invalid();
    PluginState* pluginsArray = nullptr;
    DSP::PluginSlotState* slotNode = nullptr;
    
    if (isMaster) {
        insertPluginChainHead = masterPluginSlotNode_;
        pluginsArray = masterPlugins_;
        slotNode = DSP::PluginSlotFactory::getRegistry().get(masterPluginSlotNode_);
    } else {
        auto it = tracks_.find(id.id);
        if (it == tracks_.end()) return;
        insertPluginChainHead = it->second.pipeline.trackNode;
        pluginsArray = it->second.plugins;
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(insertPluginChainHead)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(insertPluginChainHead)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    
    if (slotNode) {
        if (slotNode->slots[slotIndex].isValid()) {
            NodeID pluginNodeId = slotNode->slots[slotIndex];
            if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(pluginNodeId)) {
                void* oldInstance = pluginState->pluginInstance;
                pluginState->pluginInstance = nullptr;
                if (oldInstance) {
                    delete static_cast<Layer3::IPlugin*>(oldInstance);
                }
            }
            DSP::InsertPluginFactory::getRegistry().deallocate(pluginNodeId);
            slotNode->slots[slotIndex] = NodeID::invalid();
            slotNode->bypass[slotIndex] = false;
            
            if (pluginsArray) {
                auto& plugState = pluginsArray[slotIndex];
                plugState.pluginId = 0;
                plugState.bypassed = false;
                plugState.pluginInstance = nullptr;
                std::memset(plugState.name, 0, sizeof(plugState.name));
            }
        }
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

void TrackManagerImpl::setTrackPluginBypassed(TrackID id, uint32_t slotIndex, bool bypassed) {
    if (slotIndex >= 8) return;
    
    bool oldBypassed = false;
    bool isMaster = (id.id == 0 && id.generation == 0);
    
    if (isMaster) {
        oldBypassed = masterPlugins_[slotIndex].bypassed;
    } else {
        auto it = tracks_.find(id.id);
        if (it != tracks_.end()) {
            oldBypassed = it->second.plugins[slotIndex].bypassed;
        } else {
            return;
        }
    }
    
    setTrackPluginBypassedInternal(id, slotIndex, bypassed);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::SET_PLUGIN_BYPASS;
        delta.targetId = handleToUint64(id);
        PluginLifecyclePayload p{id, slotIndex, 0, MixerRoutingOps::SET_PLUGIN_BYPASS, 0, oldBypassed, bypassed};
        delta.oldStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.oldState, &p, sizeof(PluginLifecyclePayload));
        delta.newStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.newState, &p, sizeof(PluginLifecyclePayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackPluginBypassedInternal(TrackID id, uint32_t slotIndex, bool bypassed) {
    if (slotIndex >= 8) return;
    
    bool isMaster = (id.id == 0 && id.generation == 0);
    NodeID insertPluginChainHead = NodeID::invalid();
    PluginState* pluginsArray = nullptr;
    DSP::PluginSlotState* slotNode = nullptr;
    
    if (isMaster) {
        insertPluginChainHead = masterPluginSlotNode_;
        pluginsArray = masterPlugins_;
        slotNode = DSP::PluginSlotFactory::getRegistry().get(masterPluginSlotNode_);
    } else {
        auto it = tracks_.find(id.id);
        if (it == tracks_.end()) return;
        insertPluginChainHead = it->second.pipeline.trackNode;
        pluginsArray = it->second.plugins;
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(insertPluginChainHead)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(insertPluginChainHead)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    
    if (slotNode) {
        slotNode->bypass[slotIndex] = bypassed;
        if (slotNode->slots[slotIndex].isValid()) {
            NodeID pluginNodeId = slotNode->slots[slotIndex];
            if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(pluginNodeId)) {
                pluginState->bypass = bypassed;
                pluginState->bypassRamp.setTarget(bypassed ? 0.0f : 1.0f);
            }
            
            SystemMutation mut{};
            mut.type = 30; // NODE_STATE_CHANGE / PARAMETER_CHANGE
            mut.targetId = pluginNodeId;
            mut.priority = 128;
            mut.payload[0] = BYPASS_PARAMETER_INDEX;
            float val = bypassed ? 1.0f : 0.0f;
            std::memcpy(&mut.payload[1], &val, sizeof(float));
            if (mutationBridge_) mutationBridge_->pushMutation(mut);
            
            if (pluginsArray) {
                pluginsArray[slotIndex].bypassed = bypassed;
            }
        }
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

void TrackManagerImpl::insertTrackInstrument(TrackID id, uint32_t pluginId) {
    insertTrackInstrumentInternal(id, pluginId, 0);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::INSERT_INSTRUMENT;
        delta.targetId = handleToUint64(id);
        PluginLifecyclePayload p{id, 0xFFFFFFFFu, pluginId, MixerRoutingOps::INSERT_INSTRUMENT, 0, false, false};
        delta.oldStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.oldState, &p, sizeof(PluginLifecyclePayload));
        delta.newStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.newState, &p, sizeof(PluginLifecyclePayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::insertTrackInstrumentInternal(TrackID id, uint32_t pluginId, uint32_t stateId) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;
    
    auto desc = it->second.pipeline;
    if (!desc.instrumentSlotNode.isValid()) return;
    
    auto* slotNode = DSP::getInstrumentSlotState(desc.instrumentSlotNode);
    if (!slotNode) return;
    
    PluginDescriptor plugDesc{};
    bool found = false;
    if (pluginManager_) {
        for (const auto& p : pluginManager_->getAvailablePlugins()) {
            if (p.pluginId == pluginId) {
                plugDesc = p;
                found = true;
                break;
            }
        }
    }
    if (!found) return;
    
    auto instance = pluginManager_->instantiatePlugin(plugDesc);
    if (!instance) return;
    
    void* rawInstance = instance.release();
    
    void* oldInstance = slotNode->pluginInstance;
    slotNode->pluginInstance = nullptr;
    if (oldInstance) {
        delete static_cast<Layer3::IPlugin*>(oldInstance);
    }
    
    slotNode->reset();
    std::strncpy(slotNode->name, plugDesc.name, sizeof(slotNode->name) - 1);
    slotNode->name[sizeof(slotNode->name) - 1] = '\0';
    
    PluginFormat format = PluginFormat::NONE;
    if (plugDesc.formatFlags & PluginFormatFlags::VST3) format = PluginFormat::VST3;
    else if (plugDesc.formatFlags & PluginFormatFlags::AU) format = PluginFormat::AU;
    else if (plugDesc.formatFlags & PluginFormatFlags::CLAP) format = PluginFormat::CLAP;
    slotNode->pluginHandle = { plugDesc.pluginId, 1, format };
    
    if (stateId != 0) {
        const auto* stateBuffer = pluginStateCache_.getState(stateId);
        if (stateBuffer) {
            static_cast<Layer3::IPlugin*>(rawInstance)->loadState(stateBuffer->data(), stateBuffer->size());
        }
    }
    
    std::atomic_thread_fence(std::memory_order_release);
    slotNode->pluginInstance = rawInstance;
    
    uint32_t latencySamples = 0;
    auto* plugin = static_cast<Layer3::IPlugin*>(rawInstance);
    Layer3::IPlugin::PluginInfo plugInfo{};
    if (plugin && plugin->getInfo(plugInfo)) {
        latencySamples = plugInfo.latencySamples;
    }
    
    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            trk->latency.delaySamples = latencySamples;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            trkInst->latency.delaySamples = latencySamples;
        }
    }
    
    auto& instState = it->second.instrument;
    instState.pluginId = pluginId;
    instState.bypassed = false;
    instState.pluginInstance = rawInstance;
    std::strncpy(instState.name, plugDesc.name, sizeof(instState.name) - 1);
    instState.name[sizeof(instState.name) - 1] = '\0';
    
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

void TrackManagerImpl::removeTrackInstrument(TrackID id) {
    uint32_t stateId = 0;
    uint32_t pluginId = 0;
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        pluginId = it->second.instrument.pluginId;
        if (it->second.instrument.pluginInstance) {
            auto* plugin = static_cast<Layer3::IPlugin*>(it->second.instrument.pluginInstance);
            std::vector<uint8_t> buffer = plugin->getState();
            if (!buffer.empty()) {
                stateId = pluginStateCache_.storeState(std::move(buffer));
            }
        }
    }
    
    removeTrackInstrumentInternal(id);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::REMOVE_INSTRUMENT;
        delta.targetId = handleToUint64(id);
        PluginLifecyclePayload p{id, 0xFFFFFFFFu, pluginId, MixerRoutingOps::REMOVE_INSTRUMENT, stateId, false, false};
        delta.oldStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.oldState, &p, sizeof(PluginLifecyclePayload));
        delta.newStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.newState, &p, sizeof(PluginLifecyclePayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::removeTrackInstrumentInternal(TrackID id) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;
    
    auto desc = it->second.pipeline;
    if (!desc.instrumentSlotNode.isValid()) return;
    
    if (auto* slotNode = DSP::getInstrumentSlotState(desc.instrumentSlotNode)) {
        void* oldInstance = slotNode->pluginInstance;
        slotNode->pluginInstance = nullptr;
        if (oldInstance) {
            delete static_cast<Layer3::IPlugin*>(oldInstance);
        }
        slotNode->reset();
        
        if (desc.trackNode.isValid()) {
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
                trk->latency.delaySamples = 0;
            } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
                trkInst->latency.delaySamples = 0;
            }
        }
        
        auto& instState = it->second.instrument;
        instState.pluginId = 0;
        instState.bypassed = false;
        instState.pluginInstance = nullptr;
        std::memset(instState.name, 0, sizeof(instState.name));
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

void TrackManagerImpl::setTrackInstrumentBypassed(TrackID id, bool bypassed) {
    bool oldBypassed = false;
    auto it = tracks_.find(id.id);
    if (it != tracks_.end()) {
        oldBypassed = it->second.instrument.bypassed;
    }
    
    setTrackInstrumentBypassedInternal(id, bypassed);
    if (commandHistory_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIXER_ROUTING;
        delta.operationType = MixerRoutingOps::SET_INSTRUMENT_BYPASS;
        delta.targetId = handleToUint64(id);
        PluginLifecyclePayload p{id, 0xFFFFFFFFu, 0, MixerRoutingOps::SET_INSTRUMENT_BYPASS, 0, oldBypassed, bypassed};
        delta.oldStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.oldState, &p, sizeof(PluginLifecyclePayload));
        delta.newStateSize = sizeof(PluginLifecyclePayload);
        std::memcpy(delta.newState, &p, sizeof(PluginLifecyclePayload));
        commandHistory_->pushDelta(delta);
    }
}

void TrackManagerImpl::setTrackInstrumentBypassedInternal(TrackID id, bool bypassed) {
    auto it = tracks_.find(id.id);
    if (it == tracks_.end()) return;
    
    auto desc = it->second.pipeline;
    if (!desc.instrumentSlotNode.isValid()) return;
    
    if (auto* slotNode = DSP::getInstrumentSlotState(desc.instrumentSlotNode)) {
        slotNode->bypass = bypassed;
        slotNode->bypassRamp.setTarget(bypassed ? 0.0f : 1.0f);
        
        SystemMutation mut{};
        mut.type = 30;
        mut.targetId = desc.instrumentSlotNode;
        mut.priority = 128;
        mut.payload[0] = BYPASS_PARAMETER_INDEX;
        float val = bypassed ? 1.0f : 0.0f;
        std::memcpy(&mut.payload[1], &val, sizeof(float));
        if (mutationBridge_) mutationBridge_->pushMutation(mut);
        
        it->second.instrument.bypassed = bypassed;
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(id);
}

void TrackManagerImpl::completeTrackInstrumentInsertion(TrackID trackId, void* rawInstance, const struct PluginDescriptor& plugDesc) {
    auto it = tracks_.find(trackId.id);
    if (it == tracks_.end()) {
        if (rawInstance) {
            delete static_cast<Layer3::IPlugin*>(rawInstance);
        }
        return;
    }

    auto desc = it->second.pipeline;
    if (!desc.instrumentSlotNode.isValid()) {
        if (rawInstance) {
            delete static_cast<Layer3::IPlugin*>(rawInstance);
        }
        return;
    }

    if (auto* slotNode = DSP::getInstrumentSlotState(desc.instrumentSlotNode)) {
        void* oldInstance = slotNode->pluginInstance;
        slotNode->pluginInstance = nullptr;
        if (oldInstance) {
            delete static_cast<Layer3::IPlugin*>(oldInstance);
        }

        slotNode->reset();
        std::strncpy(slotNode->name, plugDesc.name, sizeof(slotNode->name) - 1);
        slotNode->name[sizeof(slotNode->name) - 1] = '\0';
        
        PluginFormat format = PluginFormat::NONE;
        if (plugDesc.formatFlags & PluginFormatFlags::VST3) format = PluginFormat::VST3;
        else if (plugDesc.formatFlags & PluginFormatFlags::AU) format = PluginFormat::AU;
        else if (plugDesc.formatFlags & PluginFormatFlags::CLAP) format = PluginFormat::CLAP;
        slotNode->pluginHandle = { plugDesc.pluginId, 1, format };

        std::atomic_thread_fence(std::memory_order_release);
        slotNode->pluginInstance = rawInstance; 

        uint32_t latencySamples = 0;
        if (rawInstance) {
            auto* plugin = static_cast<Layer3::IPlugin*>(rawInstance);
            Layer3::IPlugin::PluginInfo info{};
            if (plugin->getInfo(info)) {
                latencySamples = info.latencySamples;
            }
        }
        
        if (desc.trackNode.isValid()) {
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
                trk->latency.delaySamples = latencySamples;
            } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
                trkInst->latency.delaySamples = latencySamples;
            }
        }
    }

    // Update state cache
    auto& track = it->second;
    track.instrument.pluginId = plugDesc.pluginId;
    track.instrument.bypassed = false;
    track.instrument.pluginInstance = rawInstance;
    std::strncpy(track.instrument.name, plugDesc.name, sizeof(track.instrument.name) - 1);
    track.instrument.name[sizeof(track.instrument.name) - 1] = '\0';

    if (mixerRoutingCallback_) {
        mixerRoutingCallback_(trackId);
    }
}

void TrackManagerImpl::completeTrackPluginInsertion(TrackID trackId, uint32_t slotIndex, void* rawInstance, const struct PluginDescriptor& plugDesc) {
    if (slotIndex >= 8) {
        if (rawInstance) delete static_cast<Layer3::IPlugin*>(rawInstance);
        return;
    }
    
    bool isMaster = (trackId.id == 0 && trackId.generation == 0);
    NodeID insertPluginChainHead = NodeID::invalid();
    PluginState* pluginsArray = nullptr;
    DSP::PluginSlotState* slotNode = nullptr;
    
    if (isMaster) {
        insertPluginChainHead = masterPluginSlotNode_;
        pluginsArray = masterPlugins_;
        slotNode = DSP::PluginSlotFactory::getRegistry().get(masterPluginSlotNode_);
    } else {
        auto it = tracks_.find(trackId.id);
        if (it == tracks_.end()) {
            if (rawInstance) delete static_cast<Layer3::IPlugin*>(rawInstance);
            return;
        }
        insertPluginChainHead = it->second.pipeline.trackNode;
        pluginsArray = it->second.plugins;
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(insertPluginChainHead)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(insertPluginChainHead)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    
    if (!slotNode) {
        if (rawInstance) delete static_cast<Layer3::IPlugin*>(rawInstance);
        return;
    }

    auto optId = DSP::InsertPluginFactory::getRegistry().allocate();
    if (optId) {
        NodeID pluginNodeId = *optId;
        if (pluginNodeId.isValid()) {
            if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(pluginNodeId)) {
                pluginState->reset();
                pluginState->pluginInstance = rawInstance;
                
                PluginFormat format = PluginFormat::NONE;
                if (plugDesc.formatFlags & PluginFormatFlags::VST3) format = PluginFormat::VST3;
                else if (plugDesc.formatFlags & PluginFormatFlags::CLAP) format = PluginFormat::CLAP;
                else if (plugDesc.formatFlags & PluginFormatFlags::AU) format = PluginFormat::AU;
                
                pluginState->pluginHandle = { plugDesc.pluginId, 1, format };
                std::strncpy(pluginState->name, plugDesc.name, sizeof(pluginState->name) - 1);
                pluginState->name[sizeof(pluginState->name) - 1] = '\0';
            }
            
            std::atomic_thread_fence(std::memory_order_release);
            
            slotNode->slots[slotIndex] = pluginNodeId;
            slotNode->bypass[slotIndex] = false;
            
            if (pluginsArray) {
                auto& plugState = pluginsArray[slotIndex];
                plugState.pluginId = plugDesc.pluginId;
                plugState.bypassed = false;
                plugState.pluginInstance = rawInstance;
                std::strncpy(plugState.name, plugDesc.name, sizeof(plugState.name) - 1);
                plugState.name[sizeof(plugState.name) - 1] = '\0';
            }
        }
    } else {
        if (rawInstance) delete static_cast<Layer3::IPlugin*>(rawInstance);
    }
    if (mixerRoutingCallback_) mixerRoutingCallback_(trackId);
}

bool TrackManagerImpl::detectFeedbackCycle(TrackID sourceTrackId, TrackID targetTrackId) const {
    if (!sourceTrackId.isValid() || !targetTrackId.isValid()) return false;
    if (sourceTrackId == targetTrackId) return true;

    std::unordered_set<uint32_t> visited;
    std::vector<TrackID> stack;
    stack.push_back(targetTrackId);

    while (!stack.empty()) {
        TrackID curr = stack.back();
        stack.pop_back();

        if (curr == sourceTrackId) return true;

        if (visited.contains(curr.id)) continue;
        visited.insert(curr.id);

        auto it = tracks_.find(curr.id);
        if (it != tracks_.end()) {
            if (it->second.info.outputTargetTrackId.isValid()) {
                stack.push_back(it->second.info.outputTargetTrackId);
            }
        }
    }
    return false;
}

bool TrackManagerImpl::setTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex, TrackID sourceTrackId, float sendGainLinear) {
    if (slotIndex >= 8) return false;
    if (!targetTrackId.isValid() || !sourceTrackId.isValid()) return false;
    if (targetTrackId == sourceTrackId) return false;

    auto targetIt = tracks_.find(targetTrackId.id);
    if (targetIt == tracks_.end()) return false;

    auto sourceIt = tracks_.find(sourceTrackId.id);
    if (sourceIt == tracks_.end()) return false;

    if (detectFeedbackCycle(sourceTrackId, targetTrackId)) {
        return false;
    }

    NodeID sourceOutputNode = getTrackOutputNode(sourceTrackId);
    if (!sourceOutputNode.isValid()) return false;

    NodeID destPluginNode = NodeID::invalid();
    DSP::PluginSlotState* slotNode = nullptr;
    NodeID trackNode = targetIt->second.pipeline.trackNode;
    if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(trackNode)) {
        slotNode = &trk->pluginSlot;
    } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(trackNode)) {
        slotNode = &trkInst->pluginSlot;
    }

    if (slotNode && slotNode->slots[slotIndex].isValid()) {
        destPluginNode = slotNode->slots[slotIndex];
    } else {
        destPluginNode = targetIt->second.pipeline.trackNode;
    }

    clearTrackSidechainRouting(targetTrackId, slotIndex);

    targetIt->second.sidechains[slotIndex].sourceTrackId = sourceTrackId;
    targetIt->second.sidechains[slotIndex].sendGainLinear = sendGainLinear;
    targetIt->second.sidechains[slotIndex].isEnabled = true;

    targetIt->second.pipeline.sidechains[slotIndex] = {
        slotIndex,
        sourceTrackId,
        sourceOutputNode,
        destPluginNode,
        sendGainLinear,
        true
    };

    if (mutationBridge_) {
        SystemMutation mutation{};
        mutation.type = Layer2::MutationType::SIDECHAIN_CONNECT;
        mutation.sidechain.sourceNodeId = sourceOutputNode;
        mutation.sidechain.destNodeId = destPluginNode;
        mutation.sidechain.destSlotIndex = slotIndex;
        mutation.sidechain.destInputIndex = 1;
        mutation.sidechain.gain = sendGainLinear;
        mutation.sidechain.isEnabled = true;
        mutationBridge_->pushMutation(mutation);
    }

    if (mixerRoutingCallback_) mixerRoutingCallback_(targetTrackId);
    return true;
}

void TrackManagerImpl::clearTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex) {
    if (slotIndex >= 8) return;

    auto targetIt = tracks_.find(targetTrackId.id);
    if (targetIt == tracks_.end()) return;

    if (!targetIt->second.sidechains[slotIndex].isEnabled) return;

    NodeID destPluginNode = targetIt->second.pipeline.sidechains[slotIndex].destPluginNode;

    targetIt->second.sidechains[slotIndex] = {};
    targetIt->second.pipeline.sidechains[slotIndex] = {};

    if (mutationBridge_ && destPluginNode.isValid()) {
        SystemMutation mutation{};
        mutation.type = Layer2::MutationType::SIDECHAIN_DISCONNECT;
        mutation.sidechain.destNodeId = destPluginNode;
        mutation.sidechain.destSlotIndex = slotIndex;
        mutation.sidechain.destInputIndex = 1;
        mutation.sidechain.isEnabled = false;
        mutationBridge_->pushMutation(mutation);
    }

    if (mixerRoutingCallback_) mixerRoutingCallback_(targetTrackId);
}

bool TrackManagerImpl::getTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex, TrackID& outSourceTrackId, float& outSendGainLinear) const {
    if (slotIndex >= 8) return false;

    auto targetIt = tracks_.find(targetTrackId.id);
    if (targetIt == tracks_.end()) return false;

    if (!targetIt->second.sidechains[slotIndex].isEnabled) return false;

    outSourceTrackId = targetIt->second.sidechains[slotIndex].sourceTrackId;
    outSendGainLinear = targetIt->second.sidechains[slotIndex].sendGainLinear;
    return true;
}

} // namespace composition
