#include "project_state_bridge.h"
#include "ikey_signature_map.h"
#include "musical_composition/automation/automation_lane_manager_impl.h"
#include "musical_composition/automation/automation_lane_impl.h"
#include "marker_manager_impl.h"
#include "region_metadata_manager_impl.h"
#include "iproject_session.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "musical_composition/track_manager/track_manager_impl.h"
#include "musical_composition/playlist/playlist_impl.h"
#include "musical_composition/midi_sequencer/midi_sequencer_impl.h"
#include "musical_composition/musical_primitives.h"
#include "musical_composition/region_manager/region_source_manager_impl.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include "Core audio engine/plugin/placeholder_plugin.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "DSP nodes/tracks/audio_track_node.h"
#include "DSP nodes/tracks/instrument_track_node.h"
#include "musical_composition/automation/iautomation_lane.h"
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace composition {

// =============================================================================
// Path Helpers
// =============================================================================

static std::string makeRelativePath(const std::string& absolutePath, const std::string& projectPath) {
    if (absolutePath.empty() || projectPath.empty()) return absolutePath;
    try {
        std::filesystem::path pPath = std::filesystem::path(projectPath).parent_path();
        std::filesystem::path aPath(absolutePath);
        return std::filesystem::relative(aPath, pPath).string();
    } catch (...) {
        return "";
    }
}

static std::string makeAbsolutePath(const std::string& relativePath, const std::string& projectPath) {
    if (relativePath.empty() || projectPath.empty()) return relativePath;
    try {
        std::filesystem::path pPath = std::filesystem::path(projectPath).parent_path();
        return (pPath / relativePath).lexically_normal().string();
    } catch (...) {
        return relativePath;
    }
}

// =============================================================================
// Automation Helper
// =============================================================================

static std::pair<AutomationTargetType, uint8_t> resolveTargetRole(
    TrackID trackId,
    NodeID nodeId,
    ITrackManager* trackManager
) {
    if (!trackManager) {
        return { AutomationTargetType::Unknown, 0 };
    }
    
    auto desc = trackManager->getPipelineDescriptor(trackId);
    if (nodeId == desc.trackNode) {
        return { AutomationTargetType::ChannelStrip, 0 };
    }
    if (nodeId == desc.instrumentSlotNode) {
        return { AutomationTargetType::Instrument, 0 };
    }
    
    if (desc.trackNode.isValid()) {
        DSP::PluginSlotState* slotNode = nullptr;
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trkInst->pluginSlot;
        }
        if (slotNode) {
            for (uint8_t i = 0; i < DSP::PluginSlotState::MAX_SLOTS; ++i) {
                if (nodeId == slotNode->slots[i]) {
                    return { AutomationTargetType::InsertPlugin, i };
                }
            }
        }
    }
    
    return { AutomationTargetType::Unknown, 0 };
}

// =============================================================================
// Extract
// =============================================================================

ProjectState ProjectStateBridge::extract(
    const IProjectSession& session,
    ITrackManager* trackManager,
    Layer2::IStringRegistry* stringRegistry,
    const std::string& projectFilePath
) {
    ProjectState state;

    // 1. Metadata
    state.metadata = session.getMetadata();

    // 2. Global Sources
    auto* sourceMgrRaw = const_cast<IProjectSession&>(session).getRegionSourceManager();
    auto* sourceMgr = dynamic_cast<RegionSourceManagerImpl*>(sourceMgrRaw);
    if (sourceMgr) {
        auto sources = sourceMgr->getAllSources();
        state.sources.reserve(sources.size());
        for (const auto& src : sources) {
            AudioSourceState srcState;
            srcState.id = src.descriptor.sourceId.id;
            srcState.generation = src.descriptor.sourceId.generation;
            srcState.nameId = src.descriptor.nameId;
            srcState.totalLengthSamples = src.descriptor.totalLengthSamples;
            srcState.channelCount = src.descriptor.channelCount;
            srcState.sampleRate = src.descriptor.sampleRate;
            srcState.mediaId = src.descriptor.mediaId;
            srcState.filePath = src.filePath;
            srcState.relativeFilePath = makeRelativePath(src.filePath, projectFilePath);
            state.sources.push_back(srcState);
        }
    }

    // 3. Tracks
    if (trackManager) {
        std::vector<TrackID> trackIds = trackManager->getAllTrackIDs();
        state.tracks.reserve(trackIds.size());
        
        for (const TrackID& trackId : trackIds) {
            TrackCreateInfo info{};
            if (!trackManager->getTrackInfo(trackId, info)) continue;

            TrackState tState;
            tState.trackId = info.trackId;
            tState.type = info.type;
            
            std::string nameStr = "";
            if (stringRegistry && info.nameId != 0) {
                stringRegistry->getString(info.nameId, nameStr);
            }
            tState.name = nameStr;
            tState.colorARGB = info.colorARGB;
            tState.audioChannelCount = info.audioChannelCount;
            tState.isRecordArmed = info.isRecordArmed;
            tState.isInputMonitoring = info.isInputMonitoring;

            std::string comments = "";
            if (stringRegistry && info.commentsId != 0) {
                stringRegistry->getString(info.commentsId, comments);
            }
            tState.comments = comments;
            tState.outputTargetTrackId = info.outputTargetTrackId;
            tState.inputSourceIndex = info.inputSourceIndex;

            // Automation Modes & Lanes
            auto* manager = trackManager->getAutomationManager(trackId);
            auto* managerImpl = dynamic_cast<AutomationLaneManagerImpl*>(manager);
            if (managerImpl) {
                tState.automationMode = static_cast<uint8_t>(managerImpl->getAutomationMode());
                const auto& lanes = managerImpl->getLanes();
                tState.automationLanes.reserve(lanes.size());
                
                for (const auto& [target, lane] : lanes) {
                    if (!lane) continue;
                    
                    AutomationLaneState laneState;
                    auto [roleType, slotIdx] = resolveTargetRole(trackId, target.nodeId, trackManager);
                    laneState.roleType = static_cast<uint8_t>(roleType);
                    laneState.slotIdx = slotIdx;
                    laneState.semanticNameId = target.semanticNameId;
                    laneState.cachedParameterIndex = target.cachedParameterIndex;
                    laneState.subNodeId = target.subNodeId;

                    auto* laneImpl = dynamic_cast<const AutomationLaneImpl*>(lane.get());
                    if (laneImpl) {
                        laneState.points = laneImpl->getPointsList();
                    }
                    tState.automationLanes.push_back(laneState);
                }
            } else {
                tState.automationMode = static_cast<uint8_t>(AutomationMode::READ);
            }

            // Playlist regions
            auto* playlistRaw = trackManager->getPlaylist(trackId);
            auto* playlist = dynamic_cast<PlaylistImpl*>(playlistRaw);
            if (playlist) {
                tState.hasPlaylist = true;
                const auto& regions = playlist->getRawRegions();
                tState.playlistRegions.reserve(regions.size());
                for (const auto& entry : regions) {
                    PlaylistRegionState regState;
                    regState.regionId = entry.regionId;
                    regState.layer = entry.layer;
                    regState.region = entry.region;
                    tState.playlistRegions.push_back(regState);
                }
            } else {
                tState.hasPlaylist = false;
            }

            // MIDI Sequencer
            auto* seqRaw = trackManager->getMIDISequencer(trackId);
            auto* seq = dynamic_cast<MIDISequencerImpl*>(seqRaw);
            if (seq) {
                tState.hasSequencer = true;
                tState.clipPositions = seq->getRawClipPositions();
                tState.notes = seq->getRawNotes();
                tState.ccPoints = seq->getRawCCPoints();
                tState.pitchPoints = seq->getRawPitchPoints();
            } else {
                tState.hasSequencer = false;
            }

            // Plugins & State
            auto* tmImpl = dynamic_cast<TrackManagerImpl*>(trackManager);
            if (tmImpl) {
                auto trackIt = tmImpl->tracks_.find(trackId.id);
                if (trackIt != tmImpl->tracks_.end()) {
                    const auto& liveTrack = trackIt->second;
                    
                    // Instrument
                    if (liveTrack.instrument.pluginInstance) {
                        tState.hasInstrument = true;
                        tState.instrument.pluginId = liveTrack.instrument.pluginId;
                        tState.instrument.bypassed = liveTrack.instrument.bypassed;
                        tState.instrument.name = liveTrack.instrument.name;
                        
                        auto* plugin = static_cast<Layer3::IPlugin*>(liveTrack.instrument.pluginInstance);
                        tState.instrument.stateBlob = plugin->getState();
                    } else {
                        tState.hasInstrument = false;
                    }

                    // Inserts
                    tState.inserts.reserve(8);
                    for (uint32_t s = 0; s < 8; ++s) {
                        if (liveTrack.plugins[s].pluginInstance) {
                            PluginState pState;
                            pState.pluginId = liveTrack.plugins[s].pluginId;
                            pState.bypassed = liveTrack.plugins[s].bypassed;
                            pState.name = liveTrack.plugins[s].name;
                            
                            auto* plugin = static_cast<Layer3::IPlugin*>(liveTrack.plugins[s].pluginInstance);
                            pState.stateBlob = plugin->getState();
                            tState.inserts.push_back({s, pState});
                        }
                    }

                    // Sidechains
                    for (uint32_t s = 0; s < 8; ++s) {
                        if (liveTrack.sidechains[s].isEnabled) {
                            tState.sidechains.push_back({s, liveTrack.sidechains[s].sourceTrackId, liveTrack.sidechains[s].sendGainLinear});
                        }
                    }
                }
            }
            state.tracks.push_back(tState);
        }
        
        // Manually extract Master track state (TrackID{0, 0})
        {
            TrackState mState;
            mState.trackId = TrackID{0, 0};
            mState.type = TrackType::MASTER;
            mState.name = "MASTER";
            mState.colorARGB = 0xFFFF3B30;
            mState.audioChannelCount = 2;
            mState.isRecordArmed = false;
            mState.isInputMonitoring = false;
            mState.outputTargetTrackId = TrackID::invalid();
            mState.inputSourceIndex = 0;
            mState.automationMode = static_cast<uint8_t>(AutomationMode::READ);
            mState.hasPlaylist = false;
            mState.hasSequencer = false;
            mState.hasInstrument = false;
            
            auto* tmImpl = dynamic_cast<TrackManagerImpl*>(trackManager);
            if (tmImpl) {
                mState.inserts.reserve(8);
                for (uint32_t s = 0; s < 8; ++s) {
                    if (tmImpl->masterPlugins_[s].pluginInstance) {
                        PluginState pState;
                        pState.pluginId = tmImpl->masterPlugins_[s].pluginId;
                        pState.bypassed = tmImpl->masterPlugins_[s].bypassed;
                        pState.name = tmImpl->masterPlugins_[s].name;
                        
                        auto* plugin = static_cast<Layer3::IPlugin*>(tmImpl->masterPlugins_[s].pluginInstance);
                        pState.stateBlob = plugin->getState();
                        mState.inserts.push_back({s, pState});
                    }
                }
            }
            state.tracks.push_back(mState);
        }
    }

    // 4. Markers
    auto* markerMgr = const_cast<IProjectSession&>(session).getMarkerManager();
    auto* markerMgrImpl = dynamic_cast<MarkerManagerImpl*>(markerMgr);
    if (markerMgrImpl) {
        state.markers = markerMgrImpl->getMarkersDirect();
    }

    // 5. Key Signatures
    auto* keySigMap = const_cast<IProjectSession&>(session).getKeySignatureMap();
    if (keySigMap) {
        state.keySignatures = keySigMap->getEvents();
    }

    // 6. Mix Stats
    state.mixStats = session.getMixStatistics();

    // 7. Region Metadata
    auto* metaMgr = const_cast<IProjectSession&>(session).getRegionMetadataManager();
    auto* metaMgrImpl = dynamic_cast<RegionMetadataManagerImpl*>(metaMgr);
    if (metaMgrImpl) {
        const auto& map = metaMgrImpl->getMetadataDirect();
        state.regionMetadata.reserve(map.size());
        for (const auto& [id, meta] : map) {
            state.regionMetadata.push_back({ id, meta });
        }
    }

    return state;
}

// =============================================================================
// Restore
// =============================================================================

bool ProjectStateBridge::restore(
    const ProjectState& state,
    IProjectSession& outSession,
    ITrackManager* trackManager,
    Layer2::IStringRegistry* stringRegistry,
    Layer3::IPluginManager* pluginManager,
    std::vector<MissingPluginReport>* outMissingPlugins
) {
    // 1. Metadata
    outSession.setMetadata(state.metadata);

    // 2. Global Sources
    auto* sourceMgrRaw = outSession.getRegionSourceManager();
    auto* sourceMgr = dynamic_cast<RegionSourceManagerImpl*>(sourceMgrRaw);
    if (sourceMgr) {
        for (const auto& src : state.sources) {
            AudioSourceDescriptor desc{};
            desc.sourceId.id = src.id;
            desc.sourceId.generation = src.generation;
            desc.nameId = src.nameId;
            desc.totalLengthSamples = src.totalLengthSamples;
            desc.channelCount = src.channelCount;
            desc.sampleRate = src.sampleRate;
            desc.mediaId = src.mediaId;

            // Target path resolution relative/absolute
            std::string targetPath = "";
            std::string resolvedRel = makeAbsolutePath(src.relativeFilePath, state.metadata.projectName); // project path fallback
            if (std::filesystem::exists(resolvedRel)) {
                targetPath = resolvedRel;
            } else if (std::filesystem::exists(src.filePath)) {
                targetPath = src.filePath;
            } else {
                targetPath = src.filePath; // fallback path
            }

            if (stringRegistry) {
                desc.nameId = stringRegistry->registerString(targetPath);
            }
            sourceMgr->registerSource(desc, targetPath);
        }
    }

    // 3. Tracks
    if (state.tracks.size() > 0 && !trackManager) return false;

    std::vector<std::pair<TrackID, TrackID>> routingFixups;
    routingFixups.reserve(state.tracks.size());

    for (const auto& track : state.tracks) {
        TrackID restoredId;
        bool isMaster = (track.trackId.id == 0 && track.trackId.generation == 0);
        if (isMaster) {
            restoredId = track.trackId;
        } else {
            TrackCreateInfo info{};
            info.trackId = track.trackId;
            info.type = track.type;
            
            if (stringRegistry && !track.name.empty()) {
                info.nameId = stringRegistry->registerString(track.name);
            } else {
                info.nameId = 0;
            }
            info.colorARGB = track.colorARGB;
            info.audioChannelCount = track.audioChannelCount;
            info.isRecordArmed = track.isRecordArmed;
            info.isInputMonitoring = track.isInputMonitoring;

            if (stringRegistry && !track.comments.empty()) {
                info.commentsId = stringRegistry->registerString(track.comments);
            } else {
                info.commentsId = 0;
            }
            info.outputTargetTrackId = track.outputTargetTrackId;
            info.inputSourceIndex = track.inputSourceIndex;

            // Recreate the track with its original TrackID hint.
            restoredId = trackManager->createTrack(info);
            routingFixups.push_back({restoredId, info.outputTargetTrackId});
        }

        // Restore automation mode & lanes
        auto* manager = trackManager->getAutomationManager(restoredId);
        if (manager) {
            manager->setAutomationMode(static_cast<AutomationMode>(track.automationMode));
        }
        auto* managerImpl = dynamic_cast<AutomationLaneManagerImpl*>(manager);

        for (const auto& laneState : track.automationLanes) {
            // Reconstruct the new NodeID using the role and slotIndex
            NodeID newNodeId = NodeID::invalid();
            auto desc = trackManager->getPipelineDescriptor(restoredId);
            auto roleType = static_cast<AutomationTargetType>(laneState.roleType);

            if (roleType == AutomationTargetType::ChannelStrip || roleType == AutomationTargetType::Panner ||
                roleType == AutomationTargetType::PreSend || roleType == AutomationTargetType::PostSend) {
                newNodeId = desc.trackNode;
            } else if (roleType == AutomationTargetType::Instrument) {
                newNodeId = desc.instrumentSlotNode;
            } else if (roleType == AutomationTargetType::InsertPlugin) {
                if (laneState.slotIdx < 8 && desc.trackNode.isValid()) {
                    DSP::PluginSlotState* slotNode = nullptr;
                    if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
                        slotNode = &trk->pluginSlot;
                    } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
                        slotNode = &trkInst->pluginSlot;
                    }
                    if (slotNode) {
                        newNodeId = slotNode->slots[laneState.slotIdx];
                    }
                }
            }

            if (newNodeId.isValid() && managerImpl) {
                AutomationTarget target{ newNodeId, laneState.semanticNameId, laneState.cachedParameterIndex, laneState.subNodeId };
                auto* lane = dynamic_cast<AutomationLaneImpl*>(managerImpl->createLane(target));
                if (lane) {
                    lane->setPointsList(laneState.points);
                }
            }
        }

        // Restore playlist regions
        if (track.hasPlaylist) {
            auto* playlistRaw = trackManager->getPlaylist(restoredId);
            auto* playlist = dynamic_cast<PlaylistImpl*>(playlistRaw);
            if (playlist) {
                for (const auto& regState : track.playlistRegions) {
                    playlist->restoreRegion(regState.regionId, regState.region, regState.layer);
                }
            }
        }

        // Restore MIDI sequencer
        if (track.hasSequencer) {
            auto* seqRaw = trackManager->getMIDISequencer(restoredId);
            auto* seq = dynamic_cast<MIDISequencerImpl*>(seqRaw);
            if (seq) {
                for (const auto& cp : track.clipPositions) {
                    seq->restoreClipPosition(cp);
                }
                for (const auto& ne : track.notes) {
                    seq->restoreNote(ne);
                }
                for (const auto& ce : track.ccPoints) {
                    seq->restoreCCPoint(ce);
                }
                for (const auto& pe : track.pitchPoints) {
                    seq->restorePitchPoint(pe);
                }
            }
        }

        // Restore Instrument Plugin
        if (track.hasInstrument) {
            bool pluginRestored = false;
            if (pluginManager) {
                PluginDescriptor desc{};
                bool descFound = false;
                for (const auto& p : pluginManager->getAvailablePlugins()) {
                    if (p.pluginId == track.instrument.pluginId) {
                        desc = p;
                        descFound = true;
                        break;
                    }
                }
                
                if (descFound) {
                    auto plugin = pluginManager->instantiatePlugin(desc);
                    if (plugin) {
                        plugin->loadState(track.instrument.stateBlob.data(), track.instrument.stateBlob.size());
                        auto* tmImpl = dynamic_cast<TrackManagerImpl*>(trackManager);
                        if (tmImpl) {
                            tmImpl->completeTrackInstrumentInsertion(restoredId, plugin.release(), desc);
                            tmImpl->setTrackInstrumentBypassedInternal(restoredId, track.instrument.bypassed);
                            pluginRestored = true;
                        }
                    }
                }
            }
            
            if (!pluginRestored) {
                auto* placeholder = new Layer3::PlaceholderPlugin(track.instrument.pluginId, track.instrument.name, track.instrument.stateBlob);
                auto* tmImpl = dynamic_cast<TrackManagerImpl*>(trackManager);
                if (tmImpl) {
                    PluginDescriptor desc{};
                    desc.pluginId = track.instrument.pluginId;
                    std::strncpy(desc.name, track.instrument.name.c_str(), MAX_PLUGIN_NAME_LENGTH - 1);
                    desc.name[MAX_PLUGIN_NAME_LENGTH - 1] = '\0';
                    tmImpl->completeTrackInstrumentInsertion(restoredId, placeholder, desc);
                    tmImpl->setTrackInstrumentBypassedInternal(restoredId, track.instrument.bypassed);
                }
                if (outMissingPlugins) {
                    outMissingPlugins->push_back({restoredId, -1, track.instrument.pluginId, track.instrument.name});
                }
            }
        }

        // Restore Insert Plugins
        for (const auto& insert : track.inserts) {
            uint32_t slotIdx = insert.first;
            const auto& pState = insert.second;
            
            bool pluginRestored = false;
            if (pluginManager) {
                PluginDescriptor desc{};
                bool descFound = false;
                for (const auto& p : pluginManager->getAvailablePlugins()) {
                    if (p.pluginId == pState.pluginId) {
                        desc = p;
                        descFound = true;
                        break;
                    }
                }
                
                if (descFound) {
                    auto plugin = pluginManager->instantiatePlugin(desc);
                    if (plugin) {
                        plugin->loadState(pState.stateBlob.data(), pState.stateBlob.size());
                        trackManager->completeTrackPluginInsertion(restoredId, slotIdx, plugin.release(), desc);
                        auto* tmImpl = dynamic_cast<TrackManagerImpl*>(trackManager);
                        if (tmImpl) {
                            tmImpl->setTrackPluginBypassedInternal(restoredId, slotIdx, pState.bypassed);
                        }
                        pluginRestored = true;
                    }
                }
            }
            
            if (!pluginRestored) {
                auto* placeholder = new Layer3::PlaceholderPlugin(pState.pluginId, pState.name, pState.stateBlob);
                PluginDescriptor desc{};
                desc.pluginId = pState.pluginId;
                std::strncpy(desc.name, pState.name.c_str(), MAX_PLUGIN_NAME_LENGTH - 1);
                desc.name[MAX_PLUGIN_NAME_LENGTH - 1] = '\0';
                trackManager->completeTrackPluginInsertion(restoredId, slotIdx, placeholder, desc);
                auto* tmImpl = dynamic_cast<TrackManagerImpl*>(trackManager);
                if (tmImpl) {
                    tmImpl->setTrackPluginBypassedInternal(restoredId, slotIdx, pState.bypassed);
                }
                if (outMissingPlugins) {
                    outMissingPlugins->push_back({restoredId, static_cast<int>(slotIdx), pState.pluginId, pState.name});
                }
            }
        }

        // Restore Sidechains for track
        for (const auto& sc : track.sidechains) {
            trackManager->setTrackSidechainRouting(restoredId, sc.slotIndex, sc.sourceTrackId, sc.sendGainLinear);
        }
    }

    // 4. Restore Markers
    auto* markerMgr = outSession.getMarkerManager();
    auto* markerMgrImpl = dynamic_cast<MarkerManagerImpl*>(markerMgr);
    if (markerMgrImpl) {
        markerMgrImpl->setMarkersDirect(state.markers);
    }

    // 5. Restore Key Signatures
    auto* keySigMap = outSession.getKeySignatureMap();
    if (keySigMap) {
        keySigMap->setEvents(state.keySignatures);
    }

    // 6. Restore Mix Stats
    outSession.setMixStatistics(state.mixStats);

    // 7. Restore Region Metadata
    auto* metaMgr = outSession.getRegionMetadataManager();
    auto* metaMgrImpl = dynamic_cast<RegionMetadataManagerImpl*>(metaMgr);
    if (metaMgrImpl) {
        std::unordered_map<RegionID, RegionMetadata> map;
        map.reserve(state.regionMetadata.size());
        for (const auto& item : state.regionMetadata) {
            map[item.regionId] = item.metadata;
        }
        
        // Legacy compatibility sweep: guarantee every region has metadata
        if (trackManager) {
            std::vector<IPlaylist::RegionInfo> scratch(512);
            for (auto tid : trackManager->getAllTrackIDs()) {
                if (auto* playlist = trackManager->getPlaylist(tid)) {
                    uint32_t count = playlist->getAllRegions(scratch.data(), 512);
                    
                    composition::TrackCreateInfo trackInfo{};
                    uint32_t trackColor = 0xFF8B5CF6; // Default accent purple
                    if (trackManager->getTrackInfo(tid, trackInfo)) {
                        trackColor = trackInfo.colorARGB;
                    }
                    
                    for (uint32_t i = 0; i < count; ++i) {
                        RegionID rId = scratch[i].id;
                        if (map.find(rId) == map.end()) {
                            RegionMetadata defMeta{};
                            std::strncpy(defMeta.name, scratch[i].region.type == RegionType::MIDI ? "MIDI Clip" : "Audio Clip", MAX_NAME_LENGTH - 1);
                            defMeta.name[MAX_NAME_LENGTH - 1] = '\0';
                            defMeta.comment[0] = '\0';
                            defMeta.colorARGB = trackColor;
                            defMeta.hasComment = false;
                            map[rId] = defMeta;
                        }
                    }
                }
            }
        }
        metaMgrImpl->setMetadataDirect(map);
    }

    // 7. Route reconnect fixups now that all track channels exist
    for (const auto& [trackId, targetId] : routingFixups) {
        if (targetId.isValid()) {
            static_cast<TrackManagerImpl*>(trackManager)->setTrackOutputRoutingInternal(trackId, targetId);
        }
    }

    return true;
}

} // namespace composition
