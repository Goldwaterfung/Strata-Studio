#include "tracks/itrack_controller.h"
#include "tracks/track_plugin_controller.h"
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
#include "musical_composition/automation/iautomation_lane_manager.h"
#include "musical_composition/automation/automation_lane_manager_impl.h"
#include "Core audio engine/transport/itransport.h"
#include "common/math/gain.h"
#include <cstring>
#include <algorithm>
#include <thread>
#include <atomic>
#include <unordered_set>

namespace bridge {

TrackPluginController::TrackPluginController(TrackControllerContext context) : ctx_(context) {}



void TrackPluginController::insertInstrument(TrackID trackId, uint32_t pluginId) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->insertTrackInstrument(trackId, pluginId);
    }
}



void TrackPluginController::removeInstrument(TrackID trackId) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->removeTrackInstrument(trackId);
    }
}



void TrackPluginController::setInstrumentBypassed(TrackID trackId, bool bypassed) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->setTrackInstrumentBypassed(trackId, bypassed);

        auto desc = trackManager->getPipelineDescriptor(trackId);
        if (desc.instrumentSlotNode.isValid() && ctx_.recordingGateway) {
            uint64_t playhead = ctx_.transport ? ctx_.transport->getPosition() : 0;
            float bypassVal = bypassed ? 1.0f : 0.0f;
            ctx_.recordingGateway->recordValue(trackId, desc.instrumentSlotNode, BYPASS_PARAMETER_INDEX, playhead, bypassVal, ::AutomationPoint::Shape::STEP);
        }
    }
}



bool TrackPluginController::openInstrumentEditor(TrackID trackId, void* parentWindow, int& outWidth, int& outHeight) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return false;

    auto desc = trackManager->getPipelineDescriptor(trackId);
    if (!desc.instrumentSlotNode.isValid()) return false;

    if (auto* slotNode = DSP::getInstrumentSlotState(desc.instrumentSlotNode)) {
        if (slotNode->pluginInstance) {
            auto* plugin = static_cast<Layer3::IPlugin*>(slotNode->pluginInstance);
            return plugin->openEditor(parentWindow, outWidth, outHeight);
        }
    }
    return false;
}



void TrackPluginController::closeInstrumentEditor(TrackID trackId) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    auto desc = trackManager->getPipelineDescriptor(trackId);
    if (!desc.instrumentSlotNode.isValid()) return;

    if (auto* slotNode = DSP::getInstrumentSlotState(desc.instrumentSlotNode)) {
        if (slotNode->pluginInstance) {
            auto* plugin = static_cast<Layer3::IPlugin*>(slotNode->pluginInstance);
            plugin->closeEditor();
        }
    }
}



void TrackPluginController::insertPlugin(TrackID trackId, uint32_t slotIndex, uint32_t pluginId) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (slotIndex >= 8) return;

    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->insertTrackPlugin(trackId, slotIndex, pluginId);
    }
}



void TrackPluginController::removePlugin(TrackID trackId, uint32_t slotIndex) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (slotIndex >= 8) return;

    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->removeTrackPlugin(trackId, slotIndex);
    }
}



void TrackPluginController::setPluginBypassed(TrackID trackId, uint32_t slotIndex, bool bypassed) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (slotIndex >= 8) return;

    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->setTrackPluginBypassed(trackId, slotIndex, bypassed);

        auto desc = trackManager->getPipelineDescriptor(trackId);
        DSP::PluginSlotState* slotNode = nullptr;
        if (desc.trackNode.isValid()) {
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
                slotNode = &trk->pluginSlot;
            } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
                slotNode = &trkInst->pluginSlot;
            }
        }
        if (slotNode) {
            if (slotNode->slots[slotIndex].isValid() && ctx_.recordingGateway) {
                NodeID pluginNodeId = slotNode->slots[slotIndex];
                uint64_t playhead = ctx_.transport ? ctx_.transport->getPosition() : 0;
                float bypassVal = bypassed ? 1.0f : 0.0f;
                ctx_.recordingGateway->recordValue(trackId, pluginNodeId, BYPASS_PARAMETER_INDEX, playhead, bypassVal, ::AutomationPoint::Shape::STEP);
            }
        }
    }
}

std::vector<uint8_t> TrackPluginController::getPluginState(TrackID trackId, uint32_t slotIndex) const {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (slotIndex >= 8) return {};

    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return {};

    auto desc = trackManager->getPipelineDescriptor(trackId);
    DSP::PluginSlotState* slotNode = nullptr;
    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    if (!slotNode) return {};

    if (slotNode->slots[slotIndex].isValid()) {
        if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[slotIndex])) {
            if (pluginState->pluginInstance) {
                auto* plugin = static_cast<Layer3::IPlugin*>(pluginState->pluginInstance);
                return plugin->getState();
            }
        }
    }
    return {};
}

void TrackPluginController::setPluginState(TrackID trackId, uint32_t slotIndex, const std::vector<uint8_t>& state) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (slotIndex >= 8 || state.empty()) return;

    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    auto desc = trackManager->getPipelineDescriptor(trackId);
    DSP::PluginSlotState* slotNode = nullptr;
    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    if (!slotNode) return;

    if (slotNode->slots[slotIndex].isValid()) {
        if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[slotIndex])) {
            if (pluginState->pluginInstance) {
                auto* plugin = static_cast<Layer3::IPlugin*>(pluginState->pluginInstance);
                plugin->loadState(state.data(), state.size());
            }
        }
    }
}

void TrackPluginController::setPluginParameter(TrackID trackId, uint32_t slotIndex, uint32_t paramIndex, float value) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (slotIndex >= 8) return;

    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    auto desc = trackManager->getPipelineDescriptor(trackId);
    DSP::PluginSlotState* slotNode = nullptr;
    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    if (!slotNode) return;

    if (slotNode->slots[slotIndex].isValid()) {
        if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[slotIndex])) {
            if (pluginState->pluginInstance) {
                auto* plugin = static_cast<Layer3::IPlugin*>(pluginState->pluginInstance);
                plugin->setParameterValue(paramIndex, value);
            }
        }
    }
}

uint32_t TrackPluginController::getPluginIdInSlot(TrackID trackId, uint32_t slotIndex) const {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (slotIndex >= 8) return UINT32_MAX;

    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return UINT32_MAX;

    auto desc = trackManager->getPipelineDescriptor(trackId);
    DSP::PluginSlotState* slotNode = nullptr;
    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    if (!slotNode) return UINT32_MAX;

    if (slotNode->slots[slotIndex].isValid()) {
        if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[slotIndex])) {
            return pluginState->pluginHandle.id != 0 ? pluginState->pluginHandle.id : slotNode->slots[slotIndex].id;
        }
        return slotNode->slots[slotIndex].id;
    }
    return UINT32_MAX;
}

uint32_t TrackPluginController::findPluginIdByName(std::string_view name) const {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (name.empty() || !ctx_.pluginManager) return UINT32_MAX;

    const auto available = ctx_.pluginManager->getAvailablePlugins();
    for (const auto& plug : available) {
        if (name == plug.name) {
            return plug.pluginId;
        }
    }
    return UINT32_MAX;
}


bool TrackPluginController::openPluginEditor(TrackID trackId, uint32_t slotIndex, void* parentWindow, int& outWidth, int& outHeight) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (slotIndex >= 8) return false;

    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return false;

    auto desc = trackManager->getPipelineDescriptor(trackId);
    DSP::PluginSlotState* slotNode = nullptr;
    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    if (!slotNode) return false;

    if (slotNode->slots[slotIndex].isValid()) {
        if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[slotIndex])) {
            if (pluginState->pluginInstance) {
                auto* plugin = static_cast<Layer3::IPlugin*>(pluginState->pluginInstance);
                return plugin->openEditor(parentWindow, outWidth, outHeight);
            }
        }
    }
    return false;
}



void TrackPluginController::closePluginEditor(TrackID trackId, uint32_t slotIndex) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    if (slotIndex >= 8) return;

    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    auto desc = trackManager->getPipelineDescriptor(trackId);
    DSP::PluginSlotState* slotNode = nullptr;
    if (desc.trackNode.isValid()) {
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trk->pluginSlot;
        } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
            slotNode = &trkInst->pluginSlot;
        }
    }
    if (!slotNode) return;

    if (slotNode->slots[slotIndex].isValid()) {
        if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[slotIndex])) {
            if (pluginState->pluginInstance) {
                auto* plugin = static_cast<Layer3::IPlugin*>(pluginState->pluginInstance);
                plugin->closeEditor();
            }
        }
    }
}

void TrackPluginController::completeInstrumentInsertion(TrackID trackId, void* rawInstance, const PluginDescriptor& plugDesc) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->completeTrackInstrumentInsertion(trackId, rawInstance, plugDesc);
    } else if (rawInstance) {
        delete static_cast<Layer3::IPlugin*>(rawInstance);
    }
}

void TrackPluginController::subscribeToPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument, ITrackController::PluginParameterTweakedCallback cb) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    Layer3::IPlugin* plugin = nullptr;
    NodeID targetNodeId = NodeID::invalid();
    uint32_t subNodeId = 0;

    auto desc = trackManager->getPipelineDescriptor(trackId);
    if (isInstrument) {
        if (desc.instrumentSlotNode.isValid()) {
            if (auto* slotNode = DSP::getInstrumentSlotState(desc.instrumentSlotNode)) {
                plugin = static_cast<Layer3::IPlugin*>(slotNode->pluginInstance);
                targetNodeId = desc.instrumentSlotNode;
                subNodeId = 0;
            }
        }
    } else {
        if (slotIndex < 8 && desc.trackNode.isValid()) {
            DSP::PluginSlotState* slotNode = nullptr;
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
                slotNode = &trk->pluginSlot;
            } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
                slotNode = &trkInst->pluginSlot;
            }
            if (slotNode && slotNode->slots[slotIndex].isValid()) {
                if (auto* pluginState = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[slotIndex])) {
                    plugin = static_cast<Layer3::IPlugin*>(pluginState->pluginInstance);
                    targetNodeId = desc.trackNode;
                    subNodeId = slotNode->slots[slotIndex].id;
                }
            }
        }
    }

    if (!plugin) return;

    if (cb) {
        plugin->setParameterTweakedCallback([cb, trackId, slotIndex, plugin, targetNodeId, subNodeId, this](uint32_t paramIndex, float value) {
            std::string pName;
            ::ParameterInfo info{};
            if (plugin->getParameterInfo(paramIndex, info)) {
                pName = info.name;
            } else {
                pName = "Param " + std::to_string(paramIndex);
            }

            {
                std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
                LastTweakedParameter lastTweaked{};
                lastTweaked.isValid = true;
                lastTweaked.routingNodeId = targetNodeId;
                lastTweaked.subNodeId = subNodeId;
                lastTweaked.paramIndex = paramIndex;
                std::strncpy(lastTweaked.paramName, pName.c_str(), sizeof(lastTweaked.paramName) - 1);
                lastTweaked.paramName[sizeof(lastTweaked.paramName) - 1] = '\0';
                lastTweaked.lastValue = value;
                ctx_.lastTweakedCache[trackId.toRaw()] = lastTweaked;
            }

            cb(trackId, slotIndex, pName.c_str(), value);
        });
    } else {
        plugin->setParameterTweakedCallback(nullptr);
    }
}

void TrackPluginController::unsubscribeFromPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument) {
    subscribeToPluginParameterTweaks(trackId, slotIndex, isInstrument, nullptr);
}

} // namespace bridge
