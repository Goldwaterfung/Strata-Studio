#include "tracks/itrack_controller.h"
#include "tracks/track_ui_state_builder.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "DSP nodes/plugins/insert_plugin_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/audio_input/audio_input_node.h"
#include "DSP nodes/tracks/audio_track_node.h"
#include "DSP nodes/tracks/instrument_track_node.h"
#include "musical_composition/automation/automation_lane_manager_impl.h"
#include "musical_composition/playlist/iplaylist.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include "common/math/gain.h"
#include <cstring>
#include <algorithm>

namespace bridge {

TrackUIStateBuilder::TrackUIStateBuilder(TrackControllerContext context) : ctx_(context) {}

std::vector<ParameterDescriptorCacheItem> TrackUIStateBuilder::getCachedParameters(TrackID trackId) const {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto it = ctx_.trackParameterCache.find(trackId.toRaw());
    if (it != ctx_.trackParameterCache.end()) {
        return it->second;
    }
    return {};
}

uint32_t TrackUIStateBuilder::getTrackCount() const {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return 0;
    return static_cast<uint32_t>(trackManager->getAllTrackIDs().size());
}

TrackUIState TrackUIStateBuilder::getTrackState(TrackID trackId) const {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    return getTrackStateInternal(trackId);
}

TrackUIState TrackUIStateBuilder::getTrackStateInternal(TrackID trackId) const {
    TrackUIState state{};
    state.trackId = trackId;
    std::strncpy(state.name, "Untitled Track", sizeof(state.name) - 1);
    state.name[sizeof(state.name) - 1] = '\0';
    state.colorARGB = 0xFF808080;
    state.type = composition::TrackType::AUDIO;
    state.parentFolderId = TrackID::invalid();
    state.faderLeveldB = -180.0f;
    state.panPosition = 0.5f;
    state.isMuted = false;
    state.isSoloed = false;
    state.isRecordArmed = false;
    state.isInputMonitoring = false;
    state.isSelected = ctx_.trackSelection[trackId.toRaw()];
    state.meterLeftPeak = -120.0f;
    state.meterRightPeak = -120.0f;
    state.activePreFaderSendCount = 0;
    state.activePostFaderSendCount = 0;
    state.channelStripNode = NodeID::invalid();
    state.pannerNode = NodeID::invalid();
    state.automationMode = AutomationMode::OFF;

    // Master bus: TrackID{0,0} is the sentinel for the master strip.
    // Read state directly from the master channel strip DSP node and master metering.
    if (trackId.id == 0 && trackId.generation == 0) {
        std::strncpy(state.name, "MASTER", sizeof(state.name) - 1);
        state.name[sizeof(state.name) - 1] = '\0';
        state.type = composition::TrackType::MASTER;
        state.colorARGB = 0xFFFF3B30;
        state.channelStripNode = ctx_.masterChannelStripNode;

        if (ctx_.masterChannelStripNode.isValid()) {
            if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(ctx_.masterChannelStripNode)) {
                state.faderLeveldB = Math::Gain::coeffTodB(cs->targetGain.load(std::memory_order_acquire));
                state.panPosition = cs->targetPan.load(std::memory_order_acquire);
                state.isMuted = cs->mute.load(std::memory_order_acquire);
                state.isSoloed = cs->solo.load(std::memory_order_acquire);
            }
        }

        if (ctx_.meteringProvider) {
            auto level = ctx_.meteringProvider->getMasterLevels();
            state.meterLeftPeak = level.peakLeft;
            state.meterRightPeak = level.peakRight;
        }

        // Load plugins for master track
        state.activePluginCount = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            state.plugins[i].pluginNodeId = NodeID::invalid();
            state.plugins[i].bypassed = false;
            std::memset(state.plugins[i]._pad, 0, sizeof(state.plugins[i]._pad));
            std::strncpy(state.plugins[i].pluginName, "-- Empty --", sizeof(state.plugins[i].pluginName) - 1);
            state.plugins[i].pluginName[sizeof(state.plugins[i].pluginName) - 1] = '\0';
        }

        if (ctx_.masterPluginSlotNode.isValid()) {
            if (auto* slotNode = DSP::PluginSlotFactory::getRegistry().get(ctx_.masterPluginSlotNode)) {
                state.activePluginCount = 8;
                for (uint32_t i = 0; i < 8; ++i) {
                    if (slotNode->slots[i].isValid()) {
                        state.plugins[i].pluginNodeId = slotNode->slots[i];
                        state.plugins[i].bypassed = slotNode->bypass[i];
                        state.plugins[i].sidechain.hasSidechainInput = false;
                        state.plugins[i].sidechain.isConnected = false;
                        state.plugins[i].sidechain.sourceTrackId = TrackID::invalid();
                        state.plugins[i].sidechain.sendGaindB = 0.0f;
                        state.plugins[i].sidechain.sourceTrackName[0] = '\0';
                        if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[i])) {
                            if (std::strlen(pluginState->name) > 0) {
                                std::strncpy(state.plugins[i].pluginName, pluginState->name, sizeof(state.plugins[i].pluginName) - 1);
                            } else {
                                std::strncpy(state.plugins[i].pluginName, "Unknown Effect", sizeof(state.plugins[i].pluginName) - 1);
                            }
                        }
                        state.plugins[i].pluginName[sizeof(state.plugins[i].pluginName) - 1] = '\0';
                    }
                }
            }
        }

        return state;
    }

    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return state;

    composition::TrackCreateInfo info{};
    if (trackManager->getTrackInfo(trackId, info)) {
        state.type = info.type;
        state.parentFolderId = trackManager->getTrackParentFolderId(trackId);
        state.colorARGB = info.colorARGB;
        state.isRecordArmed = info.isRecordArmed;
        state.isInputMonitoring = info.isInputMonitoring;
        state.isTakesExpanded = info.isTakesExpanded;
        state.outputTargetTrackId = info.outputTargetTrackId;

        if (ctx_.stringRegistry) {
            std::string nameStr;
            if (ctx_.stringRegistry->getString(info.nameId, nameStr)) {
                std::strncpy(state.name, nameStr.c_str(), sizeof(state.name) - 1);
                state.name[sizeof(state.name) - 1] = '\0';
            }
            std::string commentsStr;
            if (info.commentsId != 0 && ctx_.stringRegistry->getString(info.commentsId, commentsStr)) {
                std::strncpy(state.comments, commentsStr.c_str(), sizeof(state.comments) - 1);
                state.comments[sizeof(state.comments) - 1] = '\0';
            } else {
                state.comments[0] = '\0';
            }
        }
    }

    auto desc = trackManager->getPipelineDescriptor(trackId);
    state.channelStripNode = desc.trackNode;
    state.pannerNode = desc.trackNode;

    if (auto* playlist = trackManager->getPlaylist(trackId)) {
        state.audioLanesCount = playlist->getMaxLayer();
    }

    if (auto* manager = trackManager->getAutomationManager(trackId)) {
        state.automationMode = manager->getAutomationMode();
    }

    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            state.faderLeveldB = Math::Gain::coeffTodB(trk->channelStrip.targetGain.load(std::memory_order_acquire));
            state.panPosition = trk->channelStrip.targetPan.load(std::memory_order_acquire);
            state.isMuted = trk->channelStrip.mute.load(std::memory_order_acquire);
            state.isSoloed = trk->channelStrip.solo.load(std::memory_order_acquire);
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            state.faderLeveldB = Math::Gain::coeffTodB(trkInst->channelStrip.targetGain.load(std::memory_order_acquire));
            state.panPosition = trkInst->channelStrip.targetPan.load(std::memory_order_acquire);
            state.isMuted = trkInst->channelStrip.mute.load(std::memory_order_acquire);
            state.isSoloed = trkInst->channelStrip.solo.load(std::memory_order_acquire);
        } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(desc.trackNode)) {
            state.faderLeveldB = Math::Gain::coeffTodB(cs->targetGain.load(std::memory_order_acquire));
            state.panPosition = cs->targetPan.load(std::memory_order_acquire);
            state.isMuted = cs->mute.load(std::memory_order_acquire);
            state.isSoloed = cs->solo.load(std::memory_order_acquire);
        }
    }

    if (ctx_.meteringProvider) {
        auto level = ctx_.meteringProvider->getTrackLevels(trackId);
        state.meterLeftPeak = level.peakLeft;
        state.meterRightPeak = level.peakRight;
    }

    // Lock state
    state.isLocked = trackManager->isTrackLocked(trackId);

    // --- Pre-fader send slots ---
    uint32_t activePreSends = 0;
    for (uint32_t i = 0; i < MAX_TRACK_SENDS; ++i) {
        auto& cache = ctx_.getSendCache(trackId, true, i);
        if (cache.destinationNodeId.isValid()) {
            SendSlotUIState& slot = state.preFaderSends[i];
            slot.sendNodeId        = desc.trackNode;
            slot.destinationNodeId = cache.destinationNodeId;
            slot.leveldB           = Math::Gain::coeffTodB(cache.gainLinear);
            slot.isEnabled         = cache.isEnabled;
            slot.panPosition       = 0.5f;
            slot._pad[0] = slot._pad[1] = slot._pad[2] = 0;
            std::strncpy(slot.destinationName, cache.destinationName.c_str(), sizeof(slot.destinationName) - 1);
            slot.destinationName[sizeof(slot.destinationName) - 1] = '\0';
            activePreSends = i + 1;
        }
    }
    state.activePreFaderSendCount = activePreSends;

    // --- Post-fader send slots ---
    uint32_t activePostSends = 0;
    for (uint32_t i = 0; i < MAX_TRACK_SENDS; ++i) {
        auto& cache = ctx_.getSendCache(trackId, false, i);
        if (cache.destinationNodeId.isValid()) {
            SendSlotUIState& slot = state.postFaderSends[i];
            slot.sendNodeId        = desc.trackNode;
            slot.destinationNodeId = cache.destinationNodeId;
            slot.panPosition       = 0.5f;
            slot.isEnabled         = cache.isEnabled;
            slot.leveldB           = Math::Gain::coeffTodB(cache.gainLinear);
            slot._pad[0] = slot._pad[1] = slot._pad[2] = 0;
            std::strncpy(slot.destinationName, cache.destinationName.c_str(), sizeof(slot.destinationName) - 1);
            slot.destinationName[sizeof(slot.destinationName) - 1] = '\0';
            activePostSends = i + 1;
        }
    }
    state.activePostFaderSendCount = activePostSends;

    // --- Input Slot (Audio / MIDI) ---
    TrackInputUIState trackInput{};
    trackInput.hasInputSlot = false;
    trackInput.mappedPhysicalInputIndex = 0xFFFFFFFF;
    trackInput.numChannels = 0;
    std::strncpy(trackInput.inputName, "None", sizeof(trackInput.inputName) - 1);
    trackInput.inputName[sizeof(trackInput.inputName) - 1] = '\0';
    state.audioInputNode = desc.audioInputNode;

    if (state.type == composition::TrackType::AUDIO) {
        if (desc.audioInputNode.isValid()) {
            trackInput.hasInputSlot = true;
            if (auto* nodeState = DSP::AudioInputFactory::getRegistry().get(desc.audioInputNode)) {
                uint32_t idx = nodeState->buffers[0].hardwareChannelIndex;
                uint32_t ch  = nodeState->buffers[0].numChannels;
                trackInput.mappedPhysicalInputIndex = (idx == 255 || ch == 0) ? 0xFFFFFFFF : idx;
                trackInput.numChannels = ch;
                if (ch == 1) {
                    std::string name = "Input " + std::to_string(idx + 1);
                    std::strncpy(trackInput.inputName, name.c_str(), sizeof(trackInput.inputName) - 1);
                } else if (ch == 2) {
                    std::string name = "Input " + std::to_string(idx + 1) + "-" + std::to_string(idx + 2);
                    std::strncpy(trackInput.inputName, name.c_str(), sizeof(trackInput.inputName) - 1);
                } else {
                    std::strncpy(trackInput.inputName, "None", sizeof(trackInput.inputName) - 1);
                }
                trackInput.inputName[sizeof(trackInput.inputName) - 1] = '\0';
            }
        }
    } else if (state.type == composition::TrackType::INSTRUMENT || state.type == composition::TrackType::MIDI) {
        trackInput.hasInputSlot = true;
        uint32_t midiPortIdx = info.inputSourceIndex;
        trackInput.mappedPhysicalInputIndex = midiPortIdx;
        trackInput.numChannels = 0;

        std::string portName = "All MIDI Inputs";
        if (midiPortIdx != 0xFFFFFFFF && ctx_.hardwareFacade) {
            auto midiPorts = ctx_.hardwareFacade->getAvailableMidiPorts();
            for (const auto& port : midiPorts) {
                if (port.portIndex == midiPortIdx) {
                    portName = std::string(port.name);
                    break;
                }
            }
        }
        std::strncpy(trackInput.inputName, portName.c_str(), sizeof(trackInput.inputName) - 1);
        trackInput.inputName[sizeof(trackInput.inputName) - 1] = '\0';
    }

    state.trackInput = trackInput;
    state.hasAudioInputSlot = trackInput.hasInputSlot;
    state.audioInput = trackInput;


    // --- Instrument slot ---
    state.hasInstrumentSlot = false;
    state.instrument.pluginNodeId = NodeID::invalid();
    state.instrument.bypassed = false;
    std::memset(state.instrument._pad, 0, sizeof(state.instrument._pad));
    std::strncpy(state.instrument.pluginName, "-- Empty --", sizeof(state.instrument.pluginName) - 1);
    state.instrument.pluginName[sizeof(state.instrument.pluginName) - 1] = '\0';

    if (desc.instrumentSlotNode.isValid()) {
        state.hasInstrumentSlot = true;
        if (auto* slotNode = DSP::getInstrumentSlotState(desc.instrumentSlotNode)) {
            if (slotNode->pluginHandle.isValid()) {
                state.instrument.pluginNodeId = desc.instrumentSlotNode;
                state.instrument.bypassed = slotNode->bypass;
                if (std::strlen(slotNode->name) > 0) {
                    std::strncpy(state.instrument.pluginName, slotNode->name, sizeof(state.instrument.pluginName) - 1);
                } else {
                    std::strncpy(state.instrument.pluginName, "Unknown Instrument", sizeof(state.instrument.pluginName) - 1);
                }
                state.instrument.pluginName[sizeof(state.instrument.pluginName) - 1] = '\0';
            }
        }
    }

    // --- Insert plugin slots ---
    state.activePluginCount = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        state.plugins[i].pluginNodeId = NodeID::invalid();
        state.plugins[i].bypassed = false;
        std::memset(state.plugins[i]._pad, 0, sizeof(state.plugins[i]._pad));
        state.plugins[i].sidechain.hasSidechainInput = false;
        state.plugins[i].sidechain.isConnected = false;
        state.plugins[i].sidechain.sourceTrackId = TrackID::invalid();
        state.plugins[i].sidechain.sendGaindB = 0.0f;
        state.plugins[i].sidechain.sourceTrackName[0] = '\0';
        std::strncpy(state.plugins[i].pluginName, "-- Empty --", sizeof(state.plugins[i].pluginName) - 1);
        state.plugins[i].pluginName[sizeof(state.plugins[i].pluginName) - 1] = '\0';
    }

    state.activePluginCount = 8;
    for (uint32_t i = 0; i < 8; ++i) {
        TrackID sourceTrackId{};
        float gainLinear = 1.0f;
        state.plugins[i].sidechain.hasSidechainInput = true;
        if (trackManager && trackManager->getTrackSidechainRouting(trackId, i, sourceTrackId, gainLinear)) {
            state.plugins[i].sidechain.isConnected = true;
            state.plugins[i].sidechain.sourceTrackId = sourceTrackId;
            state.plugins[i].sidechain.sendGaindB = Math::Gain::coeffTodB(gainLinear);

            composition::TrackCreateInfo srcInfo{};
            if (trackManager->getTrackInfo(sourceTrackId, srcInfo) && ctx_.stringRegistry && srcInfo.nameId != 0) {
                std::string nameStr;
                if (ctx_.stringRegistry->getString(srcInfo.nameId, nameStr)) {
                    std::strncpy(state.plugins[i].sidechain.sourceTrackName, nameStr.c_str(), sizeof(state.plugins[i].sidechain.sourceTrackName) - 1);
                    state.plugins[i].sidechain.sourceTrackName[sizeof(state.plugins[i].sidechain.sourceTrackName) - 1] = '\0';
                }
            }
        } else {
            state.plugins[i].sidechain.isConnected = false;
            state.plugins[i].sidechain.sourceTrackId = TrackID::invalid();
            state.plugins[i].sidechain.sendGaindB = 0.0f;
            state.plugins[i].sidechain.sourceTrackName[0] = '\0';
        }
    }

    DSP::PluginSlotState* slotNode = nullptr;
    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    if (slotNode) {
        for (uint32_t i = 0; i < 8; ++i) {
            if (slotNode->slots[i].isValid()) {
                state.plugins[i].pluginNodeId = slotNode->slots[i];
                state.plugins[i].bypassed = slotNode->bypass[i];

                if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[i])) {
                    if (std::strlen(pluginState->name) > 0) {
                        std::strncpy(state.plugins[i].pluginName, pluginState->name, sizeof(state.plugins[i].pluginName) - 1);
                    } else {
                        std::strncpy(state.plugins[i].pluginName, "Unknown Effect", sizeof(state.plugins[i].pluginName) - 1);
                    }
                }
                state.plugins[i].pluginName[sizeof(state.plugins[i].pluginName) - 1] = '\0';
            }
        }
    }

    // --- Automation Sub-Lanes ---
    state.isAutomationExpanded = ctx_.automationExpanded[trackId.toRaw()];

    auto* autoManager = trackManager ? trackManager->getAutomationManager(trackId) : nullptr;
    auto* autoManagerImpl = dynamic_cast<composition::AutomationLaneManagerImpl*>(autoManager);

    std::vector<composition::AutomationTarget> targets;
    targets.reserve(128);
    // Always include standard Volume, Pan, Mute first
    if (ctx_.stringRegistry) {
        targets.push_back({state.channelStripNode, ctx_.stringRegistry->registerString("Volume"), 0, 0});
        targets.push_back({state.channelStripNode, ctx_.stringRegistry->registerString("Pan"), 1, 0});
        targets.push_back({state.channelStripNode, ctx_.stringRegistry->registerString("Mute"), 2, 0});
    }

    if (autoManagerImpl) {
        std::vector<composition::AutomationTarget> customTargets;
        customTargets.reserve(128);
        for (const auto& [target, lane] : autoManagerImpl->getLanes()) {
            // Skip standard lanes (Volume, Pan, Mute) and Solo automation
            if (target.nodeId == state.channelStripNode && target.cachedParameterIndex < 4) {
                continue;
            }
            customTargets.push_back(target);
        }
        // Sort custom targets to ensure stable ordering
        std::sort(customTargets.begin(), customTargets.end(), [](const composition::AutomationTarget& a, const composition::AutomationTarget& b) {
            if (a.nodeId.toRaw() != b.nodeId.toRaw()) {
                return a.nodeId.toRaw() < b.nodeId.toRaw();
            }
            return a.cachedParameterIndex < b.cachedParameterIndex;
        });
        targets.insert(targets.end(), customTargets.begin(), customTargets.end());
    }

    uint32_t activeCount = static_cast<uint32_t>(targets.size());
    if (activeCount > 128) {
        activeCount = 128;
    }
    state.activeSubLaneCount = activeCount;

    auto& subLanesExpanded = ctx_.subLanesExpanded[trackId.toRaw()];
    if (subLanesExpanded.size() < activeCount) {
        subLanesExpanded.resize(activeCount, true);
    }

    for (uint32_t i = 0; i < activeCount; ++i) {
        const auto& target = targets[i];
        std::string paramName;

        auto cacheIt = ctx_.trackParameterCache.find(trackId.toRaw());
        if (cacheIt != ctx_.trackParameterCache.end()) {
            for (const auto& item : cacheIt->second) {
                if (item.routingNodeId == target.nodeId &&
                    item.subNodeId == target.subNodeId &&
                    item.parameterIndex == target.cachedParameterIndex) {
                    if (std::strlen(item.info.name) > 0) {
                        paramName = item.info.name;
                    }
                    break;
                }
            }
        }

        if (paramName.empty() && ctx_.stringRegistry) {
            ctx_.stringRegistry->getString(target.semanticNameId, paramName);
        }

        if (paramName.empty()) {
            if (target.cachedParameterIndex == 0 && target.nodeId == state.channelStripNode) {
                paramName = "Volume";
            } else if (target.cachedParameterIndex == 1 && target.nodeId == state.channelStripNode) {
                paramName = "Pan";
            } else if (target.cachedParameterIndex == 2 && target.nodeId == state.channelStripNode) {
                paramName = "Mute";
            } else {
                paramName = "Param " + std::to_string(target.cachedParameterIndex);
            }
        }

        std::strncpy(state.subLanes[i].parameterName, paramName.c_str(), sizeof(state.subLanes[i].parameterName) - 1);
        state.subLanes[i].parameterName[sizeof(state.subLanes[i].parameterName) - 1] = '\0';
        state.subLanes[i].targetNodeId = target.nodeId;
        state.subLanes[i].subNodeId = target.subNodeId;
        state.subLanes[i].parameterIndex = target.cachedParameterIndex;
        state.subLanes[i].isExpanded = subLanesExpanded[i];
        {
            auto it = ctx_.subLaneHeights.find(trackId.toRaw());
            uint32_t h = 60; // default sub-lane height
            if (it != ctx_.subLaneHeights.end() && it->second.size() > i) {
                h = it->second[i];
            }
            state.subLanes[i].heightPx = h;
        }
        state.subLanes[i].recordMode = static_cast<uint8_t>(state.automationMode);
        std::memset(state.subLanes[i].padding, 0, sizeof(state.subLanes[i].padding));
    }

    auto itLast = ctx_.lastTweakedCache.find(trackId.toRaw());
    if (itLast != ctx_.lastTweakedCache.end()) {
        state.lastTweaked = itLast->second;
    } else {
        state.lastTweaked = {};
    }

    return state;
}

std::vector<TrackUIState> TrackUIStateBuilder::getAllTracks() const {
    // Drain any instrument insertions that completed on a background thread
    

    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return {};

    std::vector<TrackID> ids = trackManager->getAllTrackIDs();
    std::vector<TrackUIState> states;
    states.reserve(ids.size());

    for (auto id : ids) {
        states.push_back(getTrackStateInternal(id));
    }

    // Sort tracks by their index position in the composition arranger
    std::sort(states.begin(), states.end(), [trackManager](const TrackUIState& a, const TrackUIState& b) {
        return trackManager->getTrackIndexPosition(a.trackId) < trackManager->getTrackIndexPosition(b.trackId);
    });

    return states;
}

TrackDynamicState TrackUIStateBuilder::getDynamicState(NodeID channelStripNode) const {
    TrackDynamicState state{};
    state.faderLeveldB = -180.0f;
    state.panPosition = 0.5f;
    state.isMuted = false;
    state.isSoloed = false;
    state._pad[0] = 0;
    state._pad[1] = 0;

    if (channelStripNode.isValid()) {
        if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(channelStripNode)) {
            state.faderLeveldB = Math::Gain::coeffTodB(cs->currentGain.load(std::memory_order_acquire));
            state.panPosition = cs->currentPan.load(std::memory_order_acquire);
            state.isMuted = cs->mute.load(std::memory_order_acquire);
            state.isSoloed = cs->solo.load(std::memory_order_acquire);
        }
    }

    return state;
}



std::vector<PluginDescriptor> TrackUIStateBuilder::getAvailablePlugins() const {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (ctx_.pluginManager) {
        return ctx_.pluginManager->getAvailablePlugins();
    }
    return {};
}

void TrackUIStateBuilder::setAutomationExpanded(TrackID id, bool expanded) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    ctx_.automationExpanded[id.toRaw()] = expanded;
}

void TrackUIStateBuilder::setAutomationSubLaneExpanded(TrackID id, uint32_t subLaneIndex, bool expanded) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto& list = ctx_.subLanesExpanded[id.toRaw()];
    if (list.size() <= subLaneIndex) {
        list.resize(subLaneIndex + 1, true);
    }
    list[subLaneIndex] = expanded;
}

void TrackUIStateBuilder::setAutomationSubLaneHeight(TrackID id, uint32_t subLaneIndex, uint32_t heightPx) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto& list = ctx_.subLaneHeights[id.toRaw()];
    if (list.size() <= subLaneIndex) {
        list.resize(subLaneIndex + 1, 60);
    }
    list[subLaneIndex] = heightPx;
}

void TrackUIStateBuilder::setTrackSelected(TrackID id, bool selected) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    ctx_.trackSelection[id.toRaw()] = selected;
}

void TrackUIStateBuilder::clearTrackSelection() {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    ctx_.trackSelection.clear();
}

void TrackUIStateBuilder::initializeTrackParameterCache(TrackID trackId, composition::ITrackManager* trackManager) {
    if (!trackManager) return;
    auto desc = trackManager->getPipelineDescriptor(trackId);
    auto& cache = ctx_.trackParameterCache[trackId.toRaw()];
    cache.clear();

    // 1. Monolithic Track Macro-Node Parameters
    if (desc.trackNode.isValid()) {
        // Volume
        {
            ParameterDescriptorCacheItem item{};
            item.routingNodeId = desc.trackNode;
            item.subNodeId = 0;
            item.parameterIndex = static_cast<uint32_t>(TrackMacroParameter::Volume);
            item.info.index = static_cast<uint32_t>(TrackMacroParameter::Volume);
            std::strncpy(item.info.name, "Volume", sizeof(item.info.name) - 1);
            item.info.minValue = SystemDefaults::ParameterMinDefault;
            item.info.maxValue = SystemDefaults::ParameterMaxDefault;
            item.info.defaultValue = SystemDefaults::VolumeNormalizedDefault;
            item.info.flags = ::ParameterInfo::IS_AUTOMATABLE;
            cache.push_back(item);
        }
        // Pan
        {
            ParameterDescriptorCacheItem item{};
            item.routingNodeId = desc.trackNode;
            item.subNodeId = 0;
            item.parameterIndex = static_cast<uint32_t>(TrackMacroParameter::Pan);
            item.info.index = static_cast<uint32_t>(TrackMacroParameter::Pan);
            std::strncpy(item.info.name, "Pan", sizeof(item.info.name) - 1);
            item.info.minValue = SystemDefaults::ParameterMinDefault;
            item.info.maxValue = SystemDefaults::ParameterMaxDefault;
            item.info.defaultValue = SystemDefaults::PanNormalizedDefault;
            item.info.flags = ::ParameterInfo::IS_AUTOMATABLE;
            cache.push_back(item);
        }
        // Mute
        {
            ParameterDescriptorCacheItem item{};
            item.routingNodeId = desc.trackNode;
            item.subNodeId = 0;
            item.parameterIndex = static_cast<uint32_t>(TrackMacroParameter::Mute);
            item.info.index = static_cast<uint32_t>(TrackMacroParameter::Mute);
            std::strncpy(item.info.name, "Mute", sizeof(item.info.name) - 1);
            item.info.minValue = SystemDefaults::ParameterMinDefault;
            item.info.maxValue = SystemDefaults::ParameterMaxDefault;
            item.info.defaultValue = SystemDefaults::MuteNormalizedDefault;
            item.info.flags = static_cast<::ParameterInfo::Flags>(::ParameterInfo::IS_AUTOMATABLE | ::ParameterInfo::IS_BOOLEAN);
            cache.push_back(item);
        }
        // Sends 0..3 Level & Pan
        for (uint32_t i = 0; i < MAX_TRACK_SENDS; ++i) {
            uint32_t sendGainParam = static_cast<uint32_t>(TrackMacroParameter::Send0Gain) + (i * 3);
            uint32_t sendPanParam  = static_cast<uint32_t>(TrackMacroParameter::Send0Pan) + (i * 3);

            ParameterDescriptorCacheItem itemGain{};
            itemGain.routingNodeId = desc.trackNode;
            itemGain.subNodeId = 0;
            itemGain.parameterIndex = sendGainParam;
            itemGain.info.index = sendGainParam;
            std::string gainName = "Send " + std::to_string(i + 1) + " Level";
            std::strncpy(itemGain.info.name, gainName.c_str(), sizeof(itemGain.info.name) - 1);
            itemGain.info.minValue = 0.0f;
            itemGain.info.maxValue = 1.0f;
            itemGain.info.defaultValue = 0.0f;
            itemGain.info.flags = ::ParameterInfo::IS_AUTOMATABLE;
            cache.push_back(itemGain);

            ParameterDescriptorCacheItem itemPan{};
            itemPan.routingNodeId = desc.trackNode;
            itemPan.subNodeId = 0;
            itemPan.parameterIndex = sendPanParam;
            itemPan.info.index = sendPanParam;
            std::string panName = "Send " + std::to_string(i + 1) + " Pan";
            std::strncpy(itemPan.info.name, panName.c_str(), sizeof(itemPan.info.name) - 1);
            itemPan.info.minValue = 0.0f;
            itemPan.info.maxValue = 1.0f;
            itemPan.info.defaultValue = 0.5f;
            itemPan.info.flags = ::ParameterInfo::IS_AUTOMATABLE;
            cache.push_back(itemPan);
        }
    }

    // 2. Instrument Node (Synth/Sampler)
    if (desc.instrumentSlotNode.isValid()) {
        if (auto* inst = DSP::getInstrumentSlotState(desc.instrumentSlotNode)) {
            // Bypass Parameter
            {
                ParameterDescriptorCacheItem item{};
                item.routingNodeId = desc.instrumentSlotNode;
                item.subNodeId = 0;
                item.parameterIndex = ::BYPASS_PARAMETER_INDEX;
                item.info.index = ::BYPASS_PARAMETER_INDEX;
                
                std::string displayName = "Instrument Bypass";
                if (std::strlen(inst->name) > 0) {
                    displayName = std::string(inst->name) + " Bypass";
                }
                std::strncpy(item.info.name, displayName.c_str(), sizeof(item.info.name) - 1);
                item.info.name[sizeof(item.info.name) - 1] = '\0';
                item.info.minValue = 0.0f;
                item.info.maxValue = 1.0f;
                item.info.defaultValue = 0.0f;
                item.info.flags = static_cast<::ParameterInfo::Flags>(::ParameterInfo::IS_AUTOMATABLE | ::ParameterInfo::IS_BOOLEAN);
                cache.push_back(item);
            }
            // Plugin Parameters
            if (inst->pluginInstance) {
                auto* plugin = static_cast<Layer3::IPlugin*>(inst->pluginInstance);
                Layer3::IPlugin::PluginInfo pInfo{};
                uint32_t numParams = 0;
                if (plugin->getInfo(pInfo)) {
                    numParams = pInfo.numParameters;
                }
                for (uint32_t p = 0; p < numParams; ++p) {
                    ::ParameterInfo info{};
                    if (plugin->getParameterInfo(p, info)) {
                        if ((info.flags & ::ParameterInfo::IS_AUTOMATABLE) != 0) {
                            ParameterDescriptorCacheItem item{};
                            item.routingNodeId = desc.instrumentSlotNode;
                            item.subNodeId = 0;
                            item.parameterIndex = p;
                            item.info = info;
                            
                            std::string displayName = info.name;
                            if (std::strlen(inst->name) > 0) {
                                displayName = std::string(inst->name) + " - " + info.name;
                            }
                            std::strncpy(item.info.name, displayName.c_str(), sizeof(item.info.name) - 1);
                            item.info.name[sizeof(item.info.name) - 1] = '\0';
                            
                            cache.push_back(item);
                        }
                    }
                }
            }
        }
    }

    // 3. Insert Plugin Slots (up to 8 in series)
    DSP::PluginSlotState* slotNode = nullptr;
    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    if (slotNode) {
        for (uint32_t i = 0; i < DSP::PluginSlotState::MAX_SLOTS; ++i) {
            if (slotNode->slots[i].isValid()) {
                if (auto* ip = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[i])) {
                    // Bypass Parameter
                    {
                        ParameterDescriptorCacheItem item{};
                        item.routingNodeId = desc.trackNode;
                        item.subNodeId = slotNode->slots[i].id;
                        item.parameterIndex = ::BYPASS_PARAMETER_INDEX;
                        item.info.index = ::BYPASS_PARAMETER_INDEX;
                        
                        std::string displayName = "FX " + std::to_string(i + 1) + " Bypass";
                        if (std::strlen(ip->name) > 0) {
                            displayName = std::string(ip->name) + " Bypass";
                        }
                        std::strncpy(item.info.name, displayName.c_str(), sizeof(item.info.name) - 1);
                        item.info.name[sizeof(item.info.name) - 1] = '\0';
                        item.info.minValue = 0.0f;
                        item.info.maxValue = 1.0f;
                        item.info.defaultValue = 0.0f;
                        item.info.flags = static_cast<::ParameterInfo::Flags>(::ParameterInfo::IS_AUTOMATABLE | ::ParameterInfo::IS_BOOLEAN);
                        cache.push_back(item);
                    }
                    // Plugin Parameters
                    if (ip->pluginInstance) {
                        auto* plugin = static_cast<Layer3::IPlugin*>(ip->pluginInstance);
                        Layer3::IPlugin::PluginInfo pInfo{};
                        uint32_t numParams = 0;
                        if (plugin->getInfo(pInfo)) {
                            numParams = pInfo.numParameters;
                        }
                        for (uint32_t p = 0; p < numParams; ++p) {
                            ::ParameterInfo info{};
                            if (plugin->getParameterInfo(p, info)) {
                                if ((info.flags & ::ParameterInfo::IS_AUTOMATABLE) != 0) {
                                    ParameterDescriptorCacheItem item{};
                                    item.routingNodeId = desc.trackNode;
                                    item.subNodeId = slotNode->slots[i].id;
                                    item.parameterIndex = p;
                                    item.info = info;
                                    
                                    std::string displayName = info.name;
                                    if (std::strlen(ip->name) > 0) {
                                        displayName = std::string(ip->name) + " - " + info.name;
                                    }
                                    std::strncpy(item.info.name, displayName.c_str(), sizeof(item.info.name) - 1);
                                    item.info.name[sizeof(item.info.name) - 1] = '\0';
                                    
                                    cache.push_back(item);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace bridge
