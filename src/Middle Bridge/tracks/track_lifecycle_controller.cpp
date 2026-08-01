#include "tracks/track_controller.h"
#include "tracks/track_lifecycle_controller.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "DSP nodes/tracks/audio_track_node.h"
#include "DSP nodes/tracks/instrument_track_node.h"
#include "DSP nodes/plugins/insert_plugin_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/latency/latency_node.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include "Core audio engine/plugin/iplugin.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/midi_sequencer/imidi_sequencer.h"
#include "musical_composition/midi_sequencer/midi_sequencer_impl.h"
#include "musical_composition/automation/iautomation_lane_manager.h"
#include "musical_composition/automation/automation_lane_manager_impl.h"
#include "musical_composition/automation/automation_lane_impl.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/interfaces/iregion_metadata_manager.h"
#include "musical_composition/command_history/icommand_history.h"
#include "Core audio engine/transport/itransport.h"
#include "common/math/gain.h"
#include <cstring>
#include <algorithm>
#include <thread>
#include <atomic>
#include <unordered_set>

namespace bridge {

TrackLifecycleController::TrackLifecycleController(TrackControllerContext context) : ctx_(context) {}



TrackID TrackLifecycleController::addAudioTrack(const char* name, uint32_t channels, uint32_t colorARGB) {

    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager || !ctx_.stringRegistry) return TrackID{0, 0};

    std::string uniqueName = getUniqueTrackName(name ? name : "Audio Track", TrackID::invalid());
    uint32_t nameId = ctx_.stringRegistry->registerString(uniqueName);
    composition::TrackCreateInfo info{};
    info.type = composition::TrackType::AUDIO;
    info.nameId = nameId;
    info.colorARGB = colorARGB;
    info.audioChannelCount = channels;

    TrackID trackId = trackManager->createTrack(info);
    
    if (trackId.isValid()) {
        if (paramCacheCb_) paramCacheCb_(trackId, trackManager);

        if (ctx_.meteringProvider) {
            auto desc = trackManager->getPipelineDescriptor(trackId);
            if (desc.trackNode.isValid()) {
                ctx_.meteringProvider->registerTrackNodeMapping(trackId, desc.trackNode);
            }
        }

        // Flaw 2 fix: eagerly create the three standard automation lanes so that
        // getLanes() is never empty for a new track. This ensures getRegionsInViewport
        // can emit AUTOMATION VisualRegions (even empty ones) from the first query.
        eagerlyCreateStandardAutomationLanes(trackId, trackManager, ctx_.stringRegistry);
    }
    
    return trackId;
}



TrackID TrackLifecycleController::addInstrumentTrack(const char* name, uint32_t colorARGB) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager || !ctx_.stringRegistry) return TrackID{0, 0};

    std::string uniqueName = getUniqueTrackName(name ? name : "Instrument Track", TrackID::invalid());
    uint32_t nameId = ctx_.stringRegistry->registerString(uniqueName);
    composition::TrackCreateInfo info{};
    info.type = composition::TrackType::INSTRUMENT;
    info.nameId = nameId;
    info.colorARGB = colorARGB;
    info.audioChannelCount = 2; // Stereo by default

    TrackID trackId = trackManager->createTrack(info);

    if (trackId.isValid()) {
        if (paramCacheCb_) paramCacheCb_(trackId, trackManager);

        if (ctx_.meteringProvider) {
            auto desc = trackManager->getPipelineDescriptor(trackId);
            if (desc.trackNode.isValid()) {
                ctx_.meteringProvider->registerTrackNodeMapping(trackId, desc.trackNode);
            }
        }

        // Flaw 2 fix: eagerly create the three standard automation lanes.
        eagerlyCreateStandardAutomationLanes(trackId, trackManager, ctx_.stringRegistry);
    }

    return trackId;
}



TrackID TrackLifecycleController::addAuxTrack(const char* name, uint32_t colorARGB) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager || !ctx_.stringRegistry) return TrackID{0, 0};

    std::string uniqueName = getUniqueTrackName(name ? name : "Aux Track", TrackID::invalid());
    uint32_t nameId = ctx_.stringRegistry->registerString(uniqueName);
    composition::TrackCreateInfo info{};
    info.type = composition::TrackType::AUX;
    info.nameId = nameId;
    info.colorARGB = colorARGB;
    info.audioChannelCount = 2; // Stereo by default for Aux

    TrackID trackId = trackManager->createTrack(info);

    if (trackId.isValid()) {
        if (paramCacheCb_) paramCacheCb_(trackId, trackManager);

        if (ctx_.meteringProvider) {
            auto desc = trackManager->getPipelineDescriptor(trackId);
            if (desc.trackNode.isValid()) {
                ctx_.meteringProvider->registerTrackNodeMapping(trackId, desc.trackNode);
            }
        }
    }

    return trackId;
}


TrackID TrackLifecycleController::addFolderTrack(const char* name, uint32_t colorARGB) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager || !ctx_.stringRegistry) return TrackID{0, 0};

    std::string uniqueName = getUniqueTrackName(name ? name : "Folder", TrackID::invalid());
    uint32_t nameId = ctx_.stringRegistry->registerString(uniqueName);
    composition::TrackCreateInfo info{};
    info.type = composition::TrackType::FOLDER;
    info.nameId = nameId;
    info.colorARGB = colorARGB;
    info.audioChannelCount = 2; // Folders sum their contents

    TrackID trackId = trackManager->createTrack(info);

    if (trackId.isValid()) {
        if (paramCacheCb_) paramCacheCb_(trackId, trackManager);

        if (ctx_.meteringProvider) {
            auto desc = trackManager->getPipelineDescriptor(trackId);
            if (desc.trackNode.isValid()) {
                ctx_.meteringProvider->registerTrackNodeMapping(trackId, desc.trackNode);
            }
        }
    }

    return trackId;
}


void TrackLifecycleController::removeTrack(TrackID trackId) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    if (ctx_.meteringProvider) {
        ctx_.meteringProvider->unregisterTrackNodeMapping(trackId);
    }
    ctx_.trackParameterCache.erase(trackId.toRaw());

    auto* activeSession = ctx_.sessionManager ? ctx_.sessionManager->getActiveSession() : nullptr;
    auto* commandHistory = activeSession ? activeSession->getCommandHistory() : nullptr;

    if (commandHistory) {
        commandHistory->beginCompound();

        // 1. Delete all MIDI notes first (this will push REMOVE_NOTE deltas)
        if (auto* seq = trackManager->getMIDISequencer(trackId)) {
            if (auto* seqImpl = dynamic_cast<composition::MIDISequencerImpl*>(seq)) {
                std::vector<composition::NoteID> noteIds;
                noteIds.reserve(seqImpl->getRawNotes().size());
                for (const auto& entry : seqImpl->getRawNotes()) {
                    noteIds.push_back(entry.noteId);
                }
                for (auto noteId : noteIds) {
                    seqImpl->removeNote(noteId);
                }
            }
        }

        // 2. Delete all automation points directly on the lanes to bypass the manager's mode guard
        if (auto* autoManager = trackManager->getAutomationManager(trackId)) {
            if (auto* autoManagerImpl = dynamic_cast<composition::AutomationLaneManagerImpl*>(autoManager)) {
                for (const auto& [target, lane] : autoManagerImpl->getLanes()) {
                    if (lane) {
                        if (auto* laneImpl = dynamic_cast<composition::AutomationLaneImpl*>(lane.get())) {
                            const auto& ptsList = laneImpl->getPointsList();
                            std::vector<uint64_t> positions;
                            positions.reserve(ptsList.size());
                            for (const auto& pt : ptsList) {
                                positions.push_back(pt.positionSample);
                            }
                            for (uint64_t pos : positions) {
                                laneImpl->removePoint(pos);
                            }
                        }
                    }
                }
            }
        }

        // 3. Delete all playlist regions (this will push REMOVE_REGION deltas)
        if (auto* playlist = trackManager->getPlaylist(trackId)) {
            static constexpr uint32_t MAX_REGIONS = 1024;
            std::vector<composition::IPlaylist::RegionInfo> regions(MAX_REGIONS);
            uint32_t count = playlist->getAllRegions(regions.data(), MAX_REGIONS);
            for (uint32_t i = 0; i < count; ++i) {
                playlist->removeRegion(regions[i].id);
            }
        }
    }

    trackManager->deleteTrack(trackId);

    if (commandHistory) {
        commandHistory->endCompound();
    }
}



void TrackLifecycleController::renameTrack(TrackID trackId, const char* name) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager || !ctx_.stringRegistry) return;

    std::string uniqueName = getUniqueTrackName(name ? name : "Track", trackId);
    uint32_t nameId = ctx_.stringRegistry->registerString(uniqueName);
    trackManager->renameTrack(trackId, nameId);
}



void TrackLifecycleController::setTrackColor(TrackID trackId, uint32_t colorARGB) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    trackManager->setTrackColor(trackId, colorARGB);

    // Sync all children regions' metadata with the new track color
    if (ctx_.sessionManager) {
        if (auto* session = ctx_.sessionManager->getActiveSession()) {
            if (auto* metaMgr = session->getRegionMetadataManager()) {
                if (auto* playlist = trackManager->getPlaylist(trackId)) {
                    std::vector<composition::IPlaylist::RegionInfo> scratch(256);
                    uint32_t count = playlist->getAllRegions(scratch.data(), 256);
                    for (uint32_t i = 0; i < count; ++i) {
                        composition::RegionMetadata meta{};
                        metaMgr->getRegionMetadata(scratch[i].id, meta);
                        meta.colorARGB = colorARGB;
                        metaMgr->setRegionMetadata(scratch[i].id, meta, true);
                    }
                }
            }
        }
    }
}



void TrackLifecycleController::moveTrack(TrackID trackId, uint32_t newPositionIndex, TrackID newParentFolderId) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    // Prevent moving a folder into itself or its descendants
    if (newParentFolderId == trackId) return;

    auto isDescendant = [&](TrackID child, TrackID potentialAncestor) {
        TrackID curr = trackManager->getTrackParentFolderId(child);
        while (curr.isValid()) {
            if (curr == potentialAncestor) return true;
            curr = trackManager->getTrackParentFolderId(curr);
        }
        return false;
    };

    if (newParentFolderId.isValid() && isDescendant(newParentFolderId, trackId)) return;

    std::vector<TrackID> ids = trackManager->getAllTrackIDs();
    std::sort(ids.begin(), ids.end(), [trackManager](TrackID a, TrackID b) {
        return trackManager->getTrackIndexPosition(a) < trackManager->getTrackIndexPosition(b);
    });

    auto it = std::find(ids.begin(), ids.end(), trackId);
    if (it == ids.end()) return;

    std::vector<TrackID> blockToMove;
    std::vector<TrackID> remainingIds;
    uint32_t adjustedTargetIndex = newPositionIndex;

    for (uint32_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == trackId || isDescendant(ids[i], trackId)) {
            blockToMove.push_back(ids[i]);
            if (i < newPositionIndex) {
                if (adjustedTargetIndex > 0) adjustedTargetIndex--;
            }
        } else {
            remainingIds.push_back(ids[i]);
        }
    }

    adjustedTargetIndex = std::min(adjustedTargetIndex, static_cast<uint32_t>(remainingIds.size()));
    remainingIds.insert(remainingIds.begin() + adjustedTargetIndex, blockToMove.begin(), blockToMove.end());
    ids = std::move(remainingIds);

    TrackID oldParentFolderId = trackManager->getTrackParentFolderId(trackId);

    if (oldParentFolderId.toRaw() != newParentFolderId.toRaw()) {
        auto desc = trackManager->getPipelineDescriptor(trackId);
        NodeID trackNodeId = desc.trackNode;
        
        composition::TrackCreateInfo info;
        trackManager->getTrackInfo(trackId, info);
        uint32_t trackNodeType = DSP::NODE_TYPE_AUDIO_TRACK;
        if (info.type == composition::TrackType::INSTRUMENT) trackNodeType = DSP::NODE_TYPE_INSTRUMENT_TRACK;

        if (trackNodeId.isValid() && ctx_.mutationBridge && !info.outputTargetTrackId.isValid()) {
            // Disconnect from old destination
            NodeID oldDestNodeId;
            uint32_t oldDestType = DSP::NODE_TYPE_AUDIO_TRACK;
            if (oldParentFolderId.isValid()) {
                oldDestNodeId = trackManager->getPipelineDescriptor(oldParentFolderId).trackNode;
                composition::TrackCreateInfo parentInfo;
                trackManager->getTrackInfo(oldParentFolderId, parentInfo);
                if (parentInfo.type == composition::TrackType::INSTRUMENT) oldDestType = DSP::NODE_TYPE_INSTRUMENT_TRACK;
            } else {
                oldDestNodeId = ctx_.masterBusNode;
                oldDestType = DSP::NODE_TYPE_BUS;
            }

            uint32_t oldDestPortBase = (oldDestType == DSP::NODE_TYPE_BUS) ? 0 : TRACK_INPUT_PLAYBACK_PORT_BASE;
            for (uint32_t ch = 0; ch < 2; ++ch) {
                if (oldDestNodeId.isValid()) {
                    SystemMutation disc{};
                    disc.type = Layer2::MutationType::NODE_DISCONNECT;
                    disc.priority = 128;
                    disc.connection.sourceNodeIndex = (trackNodeType << 16) | (trackNodeId.id & 0xFFFF);
                    disc.connection.sourcePort = ch;
                    disc.connection.destNodeIndex = (oldDestType << 16) | (oldDestNodeId.id & 0xFFFF);
                    disc.connection.destPort = oldDestPortBase + ch;
                    disc.connection.gain = 1.0f;
                    ctx_.mutationBridge->pushMutation(disc);
                }
            }

            // Connect to new destination
            NodeID newDestNodeId;
            uint32_t newDestType = DSP::NODE_TYPE_AUDIO_TRACK;
            if (newParentFolderId.isValid()) {
                newDestNodeId = trackManager->getPipelineDescriptor(newParentFolderId).trackNode;
                composition::TrackCreateInfo newParentInfo;
                trackManager->getTrackInfo(newParentFolderId, newParentInfo);
                if (newParentInfo.type == composition::TrackType::INSTRUMENT) newDestType = DSP::NODE_TYPE_INSTRUMENT_TRACK;
            } else {
                newDestNodeId = ctx_.masterBusNode;
                newDestType = DSP::NODE_TYPE_BUS;
            }

            uint32_t newDestPortBase = (newDestType == DSP::NODE_TYPE_BUS) ? 0 : TRACK_INPUT_PLAYBACK_PORT_BASE;
            if (newDestNodeId.isValid()) {
                for (uint32_t ch = 0; ch < 2; ++ch) {
                    SystemMutation conn{};
                    conn.type = Layer2::MutationType::NODE_CONNECT;
                    conn.priority = 128;
                    conn.connection.sourceNodeIndex = (trackNodeType << 16) | (trackNodeId.id & 0xFFFF);
                    conn.connection.sourcePort = ch;
                    conn.connection.destNodeIndex = (newDestType << 16) | (newDestNodeId.id & 0xFFFF);
                    conn.connection.destPort = newDestPortBase + ch;
                    conn.connection.gain = 1.0f;
                    ctx_.mutationBridge->pushMutation(conn);
                }
            }
        }
    }

    for (uint32_t i = 0; i < ids.size(); ++i) {
        TrackID id = ids[i];
        TrackID parent = (id.toRaw() == trackId.toRaw()) ? newParentFolderId : trackManager->getTrackParentFolderId(id);
        trackManager->moveTrack(id, i, parent);
    }
}



void TrackLifecycleController::setTrackParentFolder(TrackID childTrackId, TrackID parentFolderId) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    if (!parentFolderId.isValid()) {
        moveTrack(childTrackId, static_cast<uint32_t>(trackManager->getAllTrackIDs().size()), TrackID::invalid());
        return;
    }

    std::vector<TrackID> ids = trackManager->getAllTrackIDs();
    std::sort(ids.begin(), ids.end(), [trackManager](TrackID a, TrackID b) {
        return trackManager->getTrackIndexPosition(a) < trackManager->getTrackIndexPosition(b);
    });

    uint32_t insertIndex = 0;
    bool foundFolder = false;

    for (uint32_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == parentFolderId) {
            foundFolder = true;
            insertIndex = i + 1;
        } else if (foundFolder) {
            bool isDescendant = false;
            TrackID currParent = trackManager->getTrackParentFolderId(ids[i]);
            while (currParent.isValid()) {
                if (currParent == parentFolderId) {
                    isDescendant = true;
                    break;
                }
                currParent = trackManager->getTrackParentFolderId(currParent);
            }

            if (isDescendant) {
                insertIndex = i + 1;
            } else {
                break;
            }
        }
    }

    if (!foundFolder) return;

    moveTrack(childTrackId, insertIndex, parentFolderId);
}



void TrackLifecycleController::setTrackMode(TrackID trackId, composition::TrackType mode) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    // Only allow switching between Audio, Instrument, Aux, and Folder modes
    if (mode != composition::TrackType::AUDIO && 
        mode != composition::TrackType::INSTRUMENT &&
        mode != composition::TrackType::AUX &&
        mode != composition::TrackType::FOLDER) return;

    composition::TrackCreateInfo info{};
    if (!trackManager->getTrackInfo(trackId, info)) return;
    if (info.type == mode) return; // Already in the requested mode

    trackManager->setTrackType(trackId, mode);
}



TrackID TrackLifecycleController::cloneTrack(TrackID sourceId) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager || !ctx_.stringRegistry) return TrackID{0, 0};

    composition::TrackCreateInfo info{};
    if (!trackManager->getTrackInfo(sourceId, info)) return TrackID{0, 0};

    auto* activeSession = ctx_.sessionManager ? ctx_.sessionManager->getActiveSession() : nullptr;
    auto* commandHistory = activeSession ? activeSession->getCommandHistory() : nullptr;

    if (commandHistory) {
        commandHistory->beginCompound();
    }

    // Build cloned track name
    std::string srcName;
    if (ctx_.stringRegistry->getString(info.nameId, srcName)) {
        srcName += " (Clone)";
    } else {
        srcName = "Cloned Track";
    }
    std::string uniqueName = getUniqueTrackName(srcName, TrackID::invalid());
    uint32_t nameId = ctx_.stringRegistry->registerString(uniqueName);
    info.nameId = nameId;
    info.trackId = TrackID::invalid();

    TrackID newId = trackManager->createTrack(info);
    if (!newId.isValid()) {
        if (commandHistory) commandHistory->endCompound();
        return TrackID{0, 0};
    }

    // Copy channel strip state from source to clone
    auto srcDesc = trackManager->getPipelineDescriptor(sourceId);
    auto dstDesc = trackManager->getPipelineDescriptor(newId);

    if (srcDesc.trackNode.isValid() && dstDesc.trackNode.isValid()) {
        DSP::ChannelStripState* srcCS = nullptr;
        DSP::ChannelStripState* dstCS = nullptr;
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(srcDesc.trackNode)) srcCS = &trk->channelStrip;
        else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(srcDesc.trackNode)) srcCS = &trkInst->channelStrip;

        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(dstDesc.trackNode)) dstCS = &trk->channelStrip;
        else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(dstDesc.trackNode)) dstCS = &trkInst->channelStrip;

        if (srcCS && dstCS) {
            dstCS->targetGain.store(srcCS->targetGain.load(std::memory_order_acquire), std::memory_order_release);
            dstCS->gainSmoother.setTarget(srcCS->targetGain.load(std::memory_order_acquire));
            dstCS->mute.store(srcCS->mute.load(std::memory_order_acquire), std::memory_order_release);
            dstCS->solo.store(false, std::memory_order_release); // Don't clone solo state
            dstCS->targetPan.store(srcCS->targetPan.load(std::memory_order_acquire), std::memory_order_release);
            dstCS->panSmoother.setTarget(srcCS->targetPan.load(std::memory_order_acquire));
        }
    }

    // Register metering for the new track
    if (ctx_.meteringProvider && dstDesc.trackNode.isValid()) {
        ctx_.meteringProvider->registerTrackNodeMapping(newId, dstDesc.trackNode);
    }

    // 1. Copy Virtual Instrument Slot
    if (srcDesc.instrumentSlotNode.isValid() && dstDesc.instrumentSlotNode.isValid()) {
        if (auto* slotNode = DSP::getInstrumentSlotState(srcDesc.instrumentSlotNode)) {
            if (slotNode->pluginInstance) {
                auto* srcPlugin = static_cast<Layer3::IPlugin*>(slotNode->pluginInstance);
                PluginDescriptor plugDesc{};
                bool found = false;
                if (ctx_.pluginManager) {
                    for (const auto& d : ctx_.pluginManager->getAvailablePlugins()) {
                        if (d.pluginId == slotNode->pluginHandle.id) {
                            plugDesc = d;
                            found = true;
                            break;
                        }
                    }
                }
                if (found) {
                    auto instance = ctx_.pluginManager->instantiatePlugin(plugDesc);
                    if (instance) {
                        auto* dstPlugin = instance.get();
                        std::vector<uint8_t> buffer = srcPlugin->getState();
                        if (!buffer.empty()) {
                            dstPlugin->loadState(buffer.data(), buffer.size());
                        }
                        if (ctx_.facade) static_cast<TrackController*>(ctx_.facade)->completeInstrumentInsertion(newId, instance.release(), plugDesc);
                        
                        // Copy bypass state
                        if (auto* dstSlotNode = DSP::getInstrumentSlotState(dstDesc.instrumentSlotNode)) {
                            dstSlotNode->bypass = slotNode->bypass;
                        }
                    }
                }
            }
        }
    }

    // 2. Copy Insert FX Plugins Chain
    DSP::PluginSlotState* srcSlotNode = nullptr;
    if (srcDesc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(srcDesc.trackNode)) srcSlotNode = &trk->pluginSlot;
        else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(srcDesc.trackNode)) srcSlotNode = &trkInst->pluginSlot;
    }
    if (srcSlotNode) {
        for (uint32_t s = 0; s < MAX_PLUGIN_SLOTS; ++s) {
            if (srcSlotNode->slots[s].isValid()) {
                if (auto* srcPluginState = DSP::InsertPluginFactory::getRegistry().get(srcSlotNode->slots[s])) {
                    if (srcPluginState->pluginInstance) {
                        auto* srcPlugin = static_cast<Layer3::IPlugin*>(srcPluginState->pluginInstance);
                        uint32_t pluginId = srcPluginState->pluginHandle.id;
                        
                        if (ctx_.facade) ctx_.facade->insertPlugin(newId, s, pluginId);
                        
                        auto updatedDstDesc = trackManager->getPipelineDescriptor(newId);
                        DSP::PluginSlotState* dstSlotNode = nullptr;
                        if (updatedDstDesc.trackNode.isValid()) {
                            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(updatedDstDesc.trackNode)) dstSlotNode = &trk->pluginSlot;
                            else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(updatedDstDesc.trackNode)) dstSlotNode = &trkInst->pluginSlot;
                        }
                        if (dstSlotNode && dstSlotNode->slots[s].isValid()) {
                            if (auto* dstPluginState = DSP::InsertPluginFactory::getRegistry().get(dstSlotNode->slots[s])) {
                                if (dstPluginState->pluginInstance) {
                                    auto* dstPlugin = static_cast<Layer3::IPlugin*>(dstPluginState->pluginInstance);
                                    std::vector<uint8_t> buffer = srcPlugin->getState();
                                    if (!buffer.empty()) {
                                        dstPlugin->loadState(buffer.data(), buffer.size());
                                    }
                                    if (ctx_.facade) ctx_.facade->setPluginBypassed(newId, s, srcSlotNode->bypass[s]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Update updated destination track descriptor for node mapping
    auto finalDstDesc = trackManager->getPipelineDescriptor(newId);

    // 3. Copy Auxiliary Sends
    for (uint32_t i = 0; i < 4; ++i) {
        // Pre-fader sends
        {
            auto& srcCache = ctx_.getSendCache(sourceId, true, i);
            if (srcCache.destinationNodeId.isValid()) {
                if (ctx_.facade) ctx_.facade->setSendDestination(newId, true, i, srcCache.destinationNodeId);
                if (ctx_.facade) ctx_.facade->setSendGain(newId, true, i, srcCache.gainLinear);
                if (ctx_.facade) ctx_.facade->setSendEnabled(newId, true, i, srcCache.isEnabled);
            }
        }
        // Post-fader sends
        {
            auto& srcCache = ctx_.getSendCache(sourceId, false, i);
            if (srcCache.destinationNodeId.isValid()) {
                if (ctx_.facade) ctx_.facade->setSendDestination(newId, false, i, srcCache.destinationNodeId);
                if (ctx_.facade) ctx_.facade->setSendGain(newId, false, i, srcCache.gainLinear);
                if (ctx_.facade) ctx_.facade->setSendEnabled(newId, false, i, srcCache.isEnabled);
            }
        }
    }

    // 4. Copy Playlist Regions and MIDI Notes
    auto* srcPlaylist = trackManager->getPlaylist(sourceId);
    auto* dstPlaylist = trackManager->getPlaylist(newId);
    auto* srcSeq = trackManager->getMIDISequencer(sourceId);
    auto* dstSeq = trackManager->getMIDISequencer(newId);

    if (srcPlaylist && dstPlaylist) {
        static constexpr uint32_t MAX_REGIONS = 1024;
        std::vector<composition::IPlaylist::RegionInfo> regions(MAX_REGIONS);
        uint32_t count = srcPlaylist->getAllRegions(regions.data(), MAX_REGIONS);
        
        std::unordered_map<uint64_t, ClipID> clipIdMap;
        
        for (uint32_t i = 0; i < count; ++i) {
            composition::TimelineRegion region = regions[i].region;
            
            if (region.type == RegionType::MIDI) {
                uint64_t oldClipRaw = region.sourceId.toRaw();
                ClipID newClipId;
                auto itClip = clipIdMap.find(oldClipRaw);
                if (itClip != clipIdMap.end()) {
                    newClipId = itClip->second;
                } else {
                    newClipId = { ++composition::getGlobalClipIdCounter(), 1 };
                    clipIdMap[oldClipRaw] = newClipId;
                    
                    if (srcSeq && dstSeq) {
                        // Copy Notes
                        static constexpr uint32_t MAX_NOTES = 4096;
                        std::vector<composition::MIDINote> notes(MAX_NOTES);
                        uint32_t noteCount = srcSeq->getNotesInClip(ClipID::fromRaw(oldClipRaw), notes.data(), MAX_NOTES);
                        for (uint32_t n = 0; n < noteCount; ++n) {
                            dstSeq->addNote(newClipId, notes[n]);
                        }
                        
                        // Copy CC points
                        static constexpr uint32_t MAX_CC = 8192;
                        std::vector<composition::MIDICCPoint> ccPoints(MAX_CC);
                        uint32_t ccCount = srcSeq->getCCPointsInClip(ClipID::fromRaw(oldClipRaw), ccPoints.data(), MAX_CC);
                        for (uint32_t c = 0; c < ccCount; ++c) {
                            dstSeq->addCCPoint(newClipId, ccPoints[c]);
                        }
                        
                        // Copy Pitch bend points
                        static constexpr uint32_t MAX_PITCH = 4096;
                        std::vector<composition::MIDIPitchPoint> pitchPoints(MAX_PITCH);
                        uint32_t pitchCount = srcSeq->getPitchPointsInClip(ClipID::fromRaw(oldClipRaw), pitchPoints.data(), MAX_PITCH);
                        for (uint32_t p = 0; p < pitchCount; ++p) {
                            dstSeq->addPitchPoint(newClipId, pitchPoints[p]);
                        }
                    }
                }
                region.sourceId = composition::SourceID::fromRaw(newClipId.toRaw());
                
                if (dstSeq) {
                    dstSeq->updateClipPosition(newClipId, region.positionSample, region.sourceLength, region.startPosition);
                }
            }
            
            dstPlaylist->addRegion(region, regions[i].layer);
        }
    }

    // 5. Clone Automation Lanes & Points (translating target Node IDs)
    std::unordered_map<uint64_t, NodeID> nodeMap;
    if (srcDesc.sourceNode.isValid() && finalDstDesc.sourceNode.isValid()) {
        nodeMap[srcDesc.sourceNode.toRaw()] = finalDstDesc.sourceNode;
    }
    if (srcDesc.instrumentSlotNode.isValid() && finalDstDesc.instrumentSlotNode.isValid()) {
        nodeMap[srcDesc.instrumentSlotNode.toRaw()] = finalDstDesc.instrumentSlotNode;
    }
    if (srcDesc.audioInputNode.isValid() && finalDstDesc.audioInputNode.isValid()) {
        nodeMap[srcDesc.audioInputNode.toRaw()] = finalDstDesc.audioInputNode;
    }
    if (srcDesc.trackNode.isValid() && finalDstDesc.trackNode.isValid()) {
        nodeMap[srcDesc.trackNode.toRaw()] = finalDstDesc.trackNode;
    }
    if (srcDesc.sourceNode.isValid() && finalDstDesc.sourceNode.isValid()) {
        nodeMap[srcDesc.sourceNode.toRaw()] = finalDstDesc.sourceNode;
    }
    if (srcDesc.instrumentSlotNode.isValid() && finalDstDesc.instrumentSlotNode.isValid()) {
        nodeMap[srcDesc.instrumentSlotNode.toRaw()] = finalDstDesc.instrumentSlotNode;
    }
    if (srcDesc.audioInputNode.isValid() && finalDstDesc.audioInputNode.isValid()) {
        nodeMap[srcDesc.audioInputNode.toRaw()] = finalDstDesc.audioInputNode;
    }
    // Map insert plugin slots:
    DSP::PluginSlotState* mapSrcSlotNode = nullptr;
    DSP::PluginSlotState* mapDstSlotNode = nullptr;
    if (srcDesc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(srcDesc.trackNode)) mapSrcSlotNode = &trk->pluginSlot;
        else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(srcDesc.trackNode)) mapSrcSlotNode = &trkInst->pluginSlot;
    }
    if (finalDstDesc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(finalDstDesc.trackNode)) mapDstSlotNode = &trk->pluginSlot;
        else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(finalDstDesc.trackNode)) mapDstSlotNode = &trkInst->pluginSlot;
    }
    if (mapSrcSlotNode && mapDstSlotNode) {
        for (uint32_t s = 0; s < MAX_PLUGIN_SLOTS; ++s) {
            if (mapSrcSlotNode->slots[s].isValid() && mapDstSlotNode->slots[s].isValid()) {
                nodeMap[mapSrcSlotNode->slots[s].toRaw()] = mapDstSlotNode->slots[s];
            }
        }
    }

    auto* srcAutoManager = trackManager->getAutomationManager(sourceId);
    auto* dstAutoManager = trackManager->getAutomationManager(newId);
    auto* srcAutoManagerImpl = dynamic_cast<composition::AutomationLaneManagerImpl*>(srcAutoManager);
    auto* dstAutoManagerImpl = dynamic_cast<composition::AutomationLaneManagerImpl*>(dstAutoManager);

    if (srcAutoManagerImpl && dstAutoManagerImpl) {
        dstAutoManagerImpl->setAutomationMode(srcAutoManagerImpl->getAutomationMode());
        for (const auto& [srcTarget, srcLane] : srcAutoManagerImpl->getLanes()) {
            NodeID dstNodeId = NodeID::invalid();
            auto itNode = nodeMap.find(srcTarget.nodeId.toRaw());
            if (itNode != nodeMap.end()) {
                dstNodeId = itNode->second;
            } else {
                dstNodeId = srcTarget.nodeId;
            }

            composition::AutomationTarget dstTarget{
                dstNodeId,
                srcTarget.semanticNameId,
                srcTarget.cachedParameterIndex,
                srcTarget.subNodeId
            };

            auto* dstLane = dstAutoManagerImpl->createLane(dstTarget);
            if (dstLane) {
                static constexpr uint32_t MAX_POINTS = 4096;
                std::vector<composition::Point> points(MAX_POINTS);
                uint32_t ptCount = srcLane->getPoints(points.data(), MAX_POINTS);
                for (uint32_t p = 0; p < ptCount; ++p) {
                    dstLane->addPoint(points[p].positionSample, points[p].value, points[p].curveShape, points[p].tension);
                }
            }
        }
    }

    // 6. Copy lock state
    trackManager->setTrackLocked(newId, trackManager->isTrackLocked(sourceId));
    auto autoExpIt = ctx_.automationExpanded.find(sourceId.toRaw());
    if (autoExpIt != ctx_.automationExpanded.end()) {
        ctx_.automationExpanded[newId.toRaw()] = autoExpIt->second;
    }
    auto subLaneExpIt = ctx_.subLanesExpanded.find(sourceId.toRaw());
    if (subLaneExpIt != ctx_.subLanesExpanded.end()) {
        ctx_.subLanesExpanded[newId.toRaw()] = subLaneExpIt->second;
    }
    auto subLaneHIt = ctx_.subLaneHeights.find(sourceId.toRaw());
    if (subLaneHIt != ctx_.subLaneHeights.end()) {
        ctx_.subLaneHeights[newId.toRaw()] = subLaneHIt->second;
    }

    if (commandHistory) {
        commandHistory->endCompound();
    }

    return newId;
}



void TrackLifecycleController::muteAllClips(TrackID trackId, bool mute) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    auto* playlist = trackManager->getPlaylist(trackId);
    if (!playlist) return;

    static constexpr uint32_t MAX_REGIONS = 512;
    composition::IPlaylist::RegionInfo regions[MAX_REGIONS];
    uint32_t count = playlist->getAllRegions(regions, MAX_REGIONS);
    for (uint32_t i = 0; i < count; ++i) {
        playlist->setRegionMuted(regions[i].id, mute);
    }
}



std::string TrackLifecycleController::getUniqueTrackName(const std::string& baseName, TrackID excludeTrackId) const {
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager || !ctx_.stringRegistry) {
        return baseName;
    }

    std::vector<TrackID> trackIds = trackManager->getAllTrackIDs();
    std::unordered_set<std::string> existingNames;
    for (auto id : trackIds) {
        if (id == excludeTrackId) {
            continue;
        }
        composition::TrackCreateInfo info{};
        if (trackManager->getTrackInfo(id, info)) {
            std::string nameStr;
            if (ctx_.stringRegistry->getString(info.nameId, nameStr)) {
                existingNames.insert(nameStr);
            }
        }
    }

    if (existingNames.find(baseName) == existingNames.end()) {
        return baseName;
    }

    std::string rootName = baseName;
    int counter = 2;

    size_t lastSpace = baseName.find_last_of(' ');
    if (lastSpace != std::string::npos && lastSpace < baseName.length() - 1) {
        std::string suffix = baseName.substr(lastSpace + 1);
        bool isNumber = !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit);
        if (isNumber) {
            rootName = baseName.substr(0, lastSpace);
            counter = std::stoi(suffix);
        }
    }

    std::string currentTrackName;
    if (excludeTrackId.isValid()) {
        composition::TrackCreateInfo info{};
        if (trackManager->getTrackInfo(excludeTrackId, info)) {
            ctx_.stringRegistry->getString(info.nameId, currentTrackName);
        }
    }

    while (true) {
        std::string candidate = rootName + " " + std::to_string(counter);
        if (existingNames.find(candidate) == existingNames.end() && candidate != currentTrackName) {
            return candidate;
        }
        counter++;
    }
}

 void TrackLifecycleController::eagerlyCreateStandardAutomationLanes(
    TrackID trackId,
    composition::ITrackManager* trackManager,
    Layer2::IStringRegistry* stringRegistry
) {
    if (!trackManager || !stringRegistry) return;

    auto* autoManager = trackManager->getAutomationManager(trackId);
    if (!autoManager) return;

    // Resolve the track node so the lanes target the correct DSP node.
    auto desc = trackManager->getPipelineDescriptor(trackId);
    if (!desc.trackNode.isValid()) return;

    // Register the four standard semantic names and create one lane per parameter.
    // Indices must match TrackLifecycleController::getTrackStateInternal subLanes[].
    struct LaneSpec { const char* name; uint32_t paramIdx; };
    static constexpr LaneSpec kStandardLanes[3] = {
        { "Volume", 0 },
        { "Pan",    1 },
        { "Mute",   2 },
    };

    for (const auto& spec : kStandardLanes) {
        uint32_t nameId = stringRegistry->registerString(spec.name);
        composition::AutomationTarget target{ desc.trackNode, nameId, spec.paramIdx, 0 };
        if (!autoManager->getLane(target)) {
            autoManager->createLane(target);
        }
    }
}

} // namespace bridge
