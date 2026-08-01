// src/Middle Bridge/automation_controller.cpp
#include "Middle Bridge/automation/automation_controller.h"
#include "musical_composition/automation/iautomation_lane_manager.h"
#include "musical_composition/automation/automation_lane_manager_impl.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/playlist/iplaylist.h"
#include "Core audio engine/transport/itransport.h"
#include "Core audio engine/automation/iautomation_processor.h"
#include "musical_composition/automation/iautomation_capture_engine.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/plugins/insert_plugin_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "Core audio engine/plugin/iplugin.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/monitor_switch/monitor_switch_node.h"
#include "common/math/gain.h"
#include "common/dsp/curve_interpolation.h"
#include "common/dsp/automation_fsm.h"
#include <algorithm>
#include <string>
#include <vector>
#include <map>

namespace bridge {

namespace {

// Forward declaration: defined later in this anonymous namespace.
bool queryPluginParameterInfo(NodeID nodeId, uint32_t subNodeId, uint32_t parameterIndex, ::ParameterInfo& outInfo);

bool isParameterBoolean(NodeID nodeId, uint32_t subNodeId, uint32_t parameterIndex, const std::string& name) {
    if (name == "Mute" || name == "Solo" || name == "Bypass" || name == "bypass" || name == "mute" || name == "solo") {
        return true;
    }
    ::ParameterInfo paramInfo{};
    if (queryPluginParameterInfo(nodeId, subNodeId, parameterIndex, paramInfo)) {
        if ((paramInfo.flags & ::ParameterInfo::IS_BOOLEAN) != 0) {
            return true;
        }
    }
    return false;
}



float getInitialParameterValue(NodeID nodeId, uint32_t subNodeId, int32_t parameterIndex) {
    float defaultValue = SystemDefaults::ParameterMinDefault;
    if (parameterIndex == static_cast<int32_t>(DSP::ChannelStripParameter::Volume)) defaultValue = SystemDefaults::VolumeNormalizedDefault;
    else if (parameterIndex == static_cast<int32_t>(DSP::ChannelStripParameter::Pan)) defaultValue = SystemDefaults::PanNormalizedDefault;

    if (!nodeId.isValid() || parameterIndex < 0) {
        return defaultValue;
    }

    if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == static_cast<int32_t>(DSP::ChannelStripParameter::Volume)) {
            float gainLinear = cs->currentGain.load(std::memory_order_relaxed);
            return Math::Gain::linearToNormalized(gainLinear);
        } else if (parameterIndex == static_cast<int32_t>(DSP::ChannelStripParameter::Pan)) {
            return cs->currentPan.load(std::memory_order_relaxed);
        }
    }

    ::ParameterInfo paramInfo{};
    // parameterIndex is guaranteed non-negative here by the early-return guard above.
    if (queryPluginParameterInfo(nodeId, subNodeId, static_cast<uint32_t>(parameterIndex), paramInfo)) {
        return paramInfo.defaultValue;
    }
    return defaultValue;
}



bool queryPluginParameterInfo(NodeID nodeId, uint32_t subNodeId, uint32_t parameterIndex, ::ParameterInfo& outInfo) {
    if (!nodeId.isValid()) return false;

    if (subNodeId != 0) {
        if (auto* slotNode = DSP::PluginSlotFactory::getRegistry().get(nodeId)) {
            for (uint32_t i = 0; i < DSP::PluginSlotState::MAX_SLOTS; ++i) {
                if (slotNode->slots[i].isValid() && slotNode->slots[i].id == subNodeId) {
                    if (auto* ip = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[i])) {
                        if (parameterIndex == ::BYPASS_PARAMETER_INDEX) {
                            outInfo.index = ::BYPASS_PARAMETER_INDEX;
                            std::strncpy(outInfo.name, "Bypass", sizeof(outInfo.name) - 1);
                            outInfo.minValue = SystemDefaults::ParameterMinDefault;
                            outInfo.maxValue = SystemDefaults::ParameterMaxDefault;
                            outInfo.defaultValue = SystemDefaults::BypassNormalizedDefault;
                            outInfo.flags = static_cast<::ParameterInfo::Flags>(::ParameterInfo::IS_AUTOMATABLE | ::ParameterInfo::IS_BOOLEAN);
                            return true;
                        }
                        if (ip->pluginInstance) {
                            auto* plugin = static_cast<Layer3::IPlugin*>(ip->pluginInstance);
                            Layer3::IPlugin::PluginInfo pInfo;
                            uint32_t numParams = 0;
                            if (plugin->getInfo(pInfo)) {
                                numParams = pInfo.numParameters;
                            }
                            for (uint32_t p = 0; p < numParams; ++p) {
                                ::ParameterInfo info;
                                if (plugin->getParameterInfo(p, info)) {
                                    if (info.index == parameterIndex) {
                                        outInfo = info;
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    if (DSP::ChannelStripFactory::getRegistry().get(nodeId)) {
        outInfo.index = parameterIndex;
        outInfo.flags = ::ParameterInfo::IS_AUTOMATABLE;
        if (parameterIndex == 0) {
            std::strncpy(outInfo.name, "Volume", sizeof(outInfo.name) - 1);
            outInfo.minValue = SystemDefaults::ParameterMinDefault;
            outInfo.maxValue = SystemDefaults::ParameterMaxDefault;
            outInfo.defaultValue = SystemDefaults::VolumeNormalizedDefault;
        } else if (parameterIndex == 1) {
            std::strncpy(outInfo.name, "Pan", sizeof(outInfo.name) - 1);
            outInfo.minValue = SystemDefaults::ParameterMinDefault;
            outInfo.maxValue = SystemDefaults::ParameterMaxDefault;
            outInfo.defaultValue = SystemDefaults::PanNormalizedDefault;
        } else if (parameterIndex == 2) {
            std::strncpy(outInfo.name, "Mute", sizeof(outInfo.name) - 1);
            outInfo.minValue = SystemDefaults::ParameterMinDefault;
            outInfo.maxValue = SystemDefaults::ParameterMaxDefault;
            outInfo.defaultValue = SystemDefaults::MuteNormalizedDefault;
            outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_BOOLEAN);
        } else if (parameterIndex == 3) {
            std::strncpy(outInfo.name, "Solo", sizeof(outInfo.name) - 1);
            outInfo.minValue = SystemDefaults::ParameterMinDefault;
            outInfo.maxValue = SystemDefaults::ParameterMaxDefault;
            outInfo.defaultValue = SystemDefaults::SoloNormalizedDefault;
            outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_BOOLEAN);
        } else {
            return false;
        }
        return true;
    }

    if (auto* ip = DSP::InsertPluginFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == ::BYPASS_PARAMETER_INDEX) {
            outInfo.index = ::BYPASS_PARAMETER_INDEX;
            std::strncpy(outInfo.name, "Bypass", sizeof(outInfo.name) - 1);
            outInfo.minValue = 0.0f;
            outInfo.maxValue = 1.0f;
            outInfo.defaultValue = 0.0f;
            outInfo.flags = static_cast<::ParameterInfo::Flags>(::ParameterInfo::IS_AUTOMATABLE | ::ParameterInfo::IS_BOOLEAN);
            return true;
        }
        if (ip->pluginInstance) {
            auto* plugin = static_cast<Layer3::IPlugin*>(ip->pluginInstance);
            Layer3::IPlugin::PluginInfo pInfo;
            uint32_t numParams = 0;
            if (plugin->getInfo(pInfo)) {
                numParams = pInfo.numParameters;
            }
            for (uint32_t p = 0; p < numParams; ++p) {
                ::ParameterInfo info;
                if (plugin->getParameterInfo(p, info)) {
                    if (info.index == parameterIndex) {
                        outInfo = info;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    if (auto* inst = DSP::getInstrumentSlotState(nodeId)) {
        if (parameterIndex == ::BYPASS_PARAMETER_INDEX) {
            outInfo.index = ::BYPASS_PARAMETER_INDEX;
            std::strncpy(outInfo.name, "Bypass", sizeof(outInfo.name) - 1);
            outInfo.minValue = 0.0f;
            outInfo.maxValue = 1.0f;
            outInfo.defaultValue = 0.0f;
            outInfo.flags = static_cast<::ParameterInfo::Flags>(::ParameterInfo::IS_AUTOMATABLE | ::ParameterInfo::IS_BOOLEAN);
            return true;
        }
        if (inst->pluginInstance) {
            auto* plugin = static_cast<Layer3::IPlugin*>(inst->pluginInstance);
            Layer3::IPlugin::PluginInfo pInfo;
            uint32_t numParams = 0;
            if (plugin->getInfo(pInfo)) {
                numParams = pInfo.numParameters;
            }
            for (uint32_t p = 0; p < numParams; ++p) {
                ::ParameterInfo info;
                if (plugin->getParameterInfo(p, info)) {
                    if (info.index == parameterIndex) {
                        outInfo = info;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    if (DSP::PannerFactory::getRegistry().get(nodeId)) {
        outInfo.index = parameterIndex;
        outInfo.flags = ::ParameterInfo::IS_AUTOMATABLE;
        if (parameterIndex == 0) {
            std::strncpy(outInfo.name, "Pan", sizeof(outInfo.name) - 1);
            outInfo.minValue = 0.0f;
            outInfo.maxValue = 1.0f;
            outInfo.defaultValue = 0.5f;
        } else if (parameterIndex == 1) {
            std::strncpy(outInfo.name, "Width", sizeof(outInfo.name) - 1);
            outInfo.minValue = 0.0f;
            outInfo.maxValue = 1.0f;
            outInfo.defaultValue = 1.0f;
        } else if (parameterIndex == 2) {
            std::strncpy(outInfo.name, "Mode", sizeof(outInfo.name) - 1);
            outInfo.minValue = 0.0f;
            outInfo.maxValue = 1.0f;
            outInfo.defaultValue = 0.0f;
        } else {
            return false;
        }
        return true;
    }

    if (DSP::SendFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == 0) {
            outInfo.index = 0;
            std::strncpy(outInfo.name, "SendGain", sizeof(outInfo.name) - 1);
            outInfo.minValue = 0.0f;
            outInfo.maxValue = 1.0f;
            outInfo.defaultValue = 0.0f;
            outInfo.flags = ::ParameterInfo::IS_AUTOMATABLE;
            return true;
        }
        return false;
    }

    if (DSP::MonitorSwitchFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == 0) {
            outInfo.index = 0;
            std::strncpy(outInfo.name, "MonitorState", sizeof(outInfo.name) - 1);
            outInfo.minValue = 0.0f;
            outInfo.maxValue = 2.0f;
            outInfo.defaultValue = 0.0f;
            outInfo.flags = ::ParameterInfo::IS_AUTOMATABLE;
            return true;
        }
        return false;
    }

    // Fallback for mock/unregistered nodes in unit tests
    outInfo.index = parameterIndex;
    outInfo.flags = ::ParameterInfo::IS_AUTOMATABLE;
    if (parameterIndex == 0) {
        std::strncpy(outInfo.name, "Volume", sizeof(outInfo.name) - 1);
        outInfo.minValue = SystemDefaults::ParameterMinDefault;
        outInfo.maxValue = SystemDefaults::ParameterMaxDefault;
        outInfo.defaultValue = SystemDefaults::VolumeNormalizedDefault;
    } else if (parameterIndex == 1) {
        std::strncpy(outInfo.name, "Pan", sizeof(outInfo.name) - 1);
        outInfo.minValue = SystemDefaults::ParameterMinDefault;
        outInfo.maxValue = SystemDefaults::ParameterMaxDefault;
        outInfo.defaultValue = SystemDefaults::PanNormalizedDefault;
    } else if (parameterIndex == 2) {
        std::strncpy(outInfo.name, "Mute", sizeof(outInfo.name) - 1);
        outInfo.minValue = SystemDefaults::ParameterMinDefault;
        outInfo.maxValue = SystemDefaults::ParameterMaxDefault;
        outInfo.defaultValue = SystemDefaults::MuteNormalizedDefault;
        outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_BOOLEAN);
    } else if (parameterIndex == 3) {
        std::strncpy(outInfo.name, "Solo", sizeof(outInfo.name) - 1);
        outInfo.minValue = SystemDefaults::ParameterMinDefault;
        outInfo.maxValue = SystemDefaults::ParameterMaxDefault;
        outInfo.defaultValue = SystemDefaults::SoloNormalizedDefault;
        outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_BOOLEAN);
    } else {
        return false;
    }
    return true;
}

} // namespace

AutomationController::AutomationController(
    ISessionManager* sessionManager,
    Layer2::IStringRegistry* stringRegistry,
    Layer3::ITransport* transport,
    composition::IAutomationCaptureEngine* captureEngine,
    Layer3::IAutomationProcessor* processor,
    DSP::ITouchStateMonitor* touchStateMonitor,
    IAutomationRecordingGateway* recordingGateway
) : sessionManager_(sessionManager),
    stringRegistry_(stringRegistry),
    transport_(transport),
    captureEngine_(captureEngine),
    processor_(processor),
    touchStateMonitor_(touchStateMonitor),
    recordingGateway_(recordingGateway) {
    if (sessionManager_) {
        sessionManager_->registerChangeListener(this);
    }
}

AutomationController::~AutomationController() {
    if (sessionManager_) {
        sessionManager_->unregisterChangeListener(this);
    }
}

composition::ITrackManager* AutomationController::getTrackManager() const {
    if (sessionManager_) {
        if (auto* session = sessionManager_->getActiveSession()) {
            return session->getTrackManager();
        }
    }
    return nullptr;
}

std::string AutomationController::queryPluginParameterName(TrackID trackId, NodeID nodeId, uint32_t subNodeId, uint32_t parameterIndex) const {
    if (!nodeId.isValid()) return "";

    if (subNodeId != 0) {
        if (auto* slotNode = DSP::PluginSlotFactory::getRegistry().get(nodeId)) {
            for (uint32_t i = 0; i < DSP::PluginSlotState::MAX_SLOTS; ++i) {
                if (slotNode->slots[i].isValid() && slotNode->slots[i].id == subNodeId) {
                    if (auto* ip = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[i])) {
                        if (parameterIndex == ::BYPASS_PARAMETER_INDEX) {
                            return "Bypass";
                        }
                        if (ip->pluginInstance) {
                            auto* plugin = static_cast<Layer3::IPlugin*>(ip->pluginInstance);
                            Layer3::IPlugin::PluginInfo pInfo;
                            uint32_t numParams = 0;
                            if (plugin->getInfo(pInfo)) {
                                numParams = pInfo.numParameters;
                            }
                            for (uint32_t p = 0; p < numParams; ++p) {
                                ::ParameterInfo paramInfo{};
                                if (plugin->getParameterInfo(p, paramInfo)) {
                                    if (p == parameterIndex || paramInfo.index == parameterIndex) {
                                        return paramInfo.name;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    auto* trackManager = getTrackManager();
    if (trackManager && trackId.isValid()) {
        auto desc = trackManager->getPipelineDescriptor(trackId);
        if (desc.trackNode == nodeId) {
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Volume)) return "Volume";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Pan))    return "Pan";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Mute))   return "Mute";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Solo))   return "Solo";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Send0Gain)) return "Send 1 Level";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Send0Pan))  return "Send 1 Pan";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Send1Gain)) return "Send 2 Level";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Send1Pan))  return "Send 2 Pan";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Send2Gain)) return "Send 3 Level";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Send2Pan))  return "Send 3 Pan";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Send3Gain)) return "Send 4 Level";
            if (parameterIndex == static_cast<uint32_t>(TrackMacroParameter::Send3Pan))  return "Send 4 Pan";
        }
    }

    if (DSP::ChannelStripFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Volume)) return "Volume";
        if (parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Pan))    return "Pan";
        if (parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Mute))   return "Mute";
        if (parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Solo))   return "Solo";
        return "";
    }

    if (auto* ip = DSP::InsertPluginFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == ::BYPASS_PARAMETER_INDEX) {
            return "Bypass";
        }
        if (ip->pluginInstance) {
            auto* plugin = static_cast<Layer3::IPlugin*>(ip->pluginInstance);
            Layer3::IPlugin::PluginInfo pInfo;
            uint32_t numParams = 0;
            if (plugin->getInfo(pInfo)) {
                numParams = pInfo.numParameters;
            }
            for (uint32_t p = 0; p < numParams; ++p) {
                ::ParameterInfo paramInfo{};
                if (plugin->getParameterInfo(p, paramInfo)) {
                    if (p == parameterIndex || paramInfo.index == parameterIndex) {
                        return paramInfo.name;
                    }
                }
            }
        }
        return "";
    }

    if (auto* inst = DSP::getInstrumentSlotState(nodeId)) {
        if (parameterIndex == ::BYPASS_PARAMETER_INDEX) {
            return "Bypass";
        }
        if (inst->pluginInstance) {
            auto* plugin = static_cast<Layer3::IPlugin*>(inst->pluginInstance);
            Layer3::IPlugin::PluginInfo pInfo;
            uint32_t numParams = 0;
            if (plugin->getInfo(pInfo)) {
                numParams = pInfo.numParameters;
            }
            for (uint32_t p = 0; p < numParams; ++p) {
                ::ParameterInfo paramInfo{};
                if (plugin->getParameterInfo(p, paramInfo)) {
                    if (p == parameterIndex || paramInfo.index == parameterIndex) {
                        return paramInfo.name;
                    }
                }
            }
        }
        return "";
    }

    if (DSP::PannerFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == 0) return "Pan";
        if (parameterIndex == 1) return "Width";
        if (parameterIndex == 2) return "Mode";
        return "";
    }

    if (DSP::MonitorSwitchFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == 0) return "MonitorState";
        return "";
    }

    return "";
}

void AutomationController::onSessionChanging() {
    if (captureEngine_) {
        captureEngine_->setActiveSession(nullptr, nullptr);
    }
    activeTrackId_ = TrackID{0, 0};
    activeNodeId_ = NodeID::invalid();
    activeTargetSubNodeId_ = 0;
    activeParameterIndex_ = -1;
    cachedParameterStringIds_.clear();
    pointsScratch_.clear();
}

void AutomationController::onSessionChanged(composition::IProjectSession* session) {
    if (captureEngine_) {
        if (session) {
            captureEngine_->setActiveSession(session->getTrackManager(), session->getCommandHistory());
        } else {
            captureEngine_->setActiveSession(nullptr, nullptr);
        }
    }
    if (!session || !processor_) return;

    auto* trackManager = session->getTrackManager();
    if (!trackManager) return;

    std::vector<TrackID> trackIds = trackManager->getAllTrackIDs();
    for (const auto& trackId : trackIds) {
        auto* manager = trackManager->getAutomationManager(trackId);
        if (!manager) continue;

        auto* managerImpl = dynamic_cast<composition::AutomationLaneManagerImpl*>(manager);
        if (!managerImpl) continue;

        const auto& lanes = managerImpl->getLanes();
        for (const auto& [target, lane] : lanes) {
            if (!lane) continue;
            compileAndPushPoints(target, lane.get());
        }
    }
}

void AutomationController::selectActiveAutomationLane(TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, int32_t parameterIndex) {
    activeTrackId_ = trackId;
    activeNodeId_ = routingNodeId;
    activeTargetSubNodeId_ = subNodeId;
    activeParameterIndex_ = parameterIndex;
}

void AutomationController::getActiveAutomationLane(
    TrackID& outTrackId,
    NodeID& outTargetNodeId,
    uint32_t& outSubNodeId,
    int32_t& outParameterIndex) const
{
    outTrackId = activeTrackId_;
    outTargetNodeId = activeNodeId_;
    outSubNodeId = activeTargetSubNodeId_;
    outParameterIndex = activeParameterIndex_;
}

composition::AutomationTarget AutomationController::resolveActiveTarget() const {
    uint32_t semanticNameId = 0;
    auto key = std::make_pair(activeNodeId_.toRaw(), activeParameterIndex_);
    auto it = cachedParameterStringIds_.find(key);
    if (it != cachedParameterStringIds_.end()) {
        semanticNameId = it->second;
    } else if (stringRegistry_ && activeParameterIndex_ >= 0) {
        std::string realName = queryPluginParameterName(activeTrackId_, activeNodeId_, activeTargetSubNodeId_, static_cast<uint32_t>(activeParameterIndex_));
        if (realName.empty()) {
            realName = "Param_" + std::to_string(activeParameterIndex_);
        }
        semanticNameId = stringRegistry_->registerString(realName);
        cachedParameterStringIds_[key] = semanticNameId;
    }
    return { activeNodeId_, semanticNameId, static_cast<uint32_t>(activeParameterIndex_), activeTargetSubNodeId_ };
}

uint32_t AutomationController::fetchPointsIntoScratch(composition::IAutomationLane* lane) const {
    if (!lane) return 0;
    uint32_t capacity = 4096;
    if (pointsScratch_.size() < capacity) {
        pointsScratch_.resize(capacity);
    }
    uint32_t count = lane->getPoints(pointsScratch_.data(), static_cast<uint32_t>(pointsScratch_.size()));
    while (count == pointsScratch_.size()) {
        pointsScratch_.resize(pointsScratch_.size() * 2);
        count = lane->getPoints(pointsScratch_.data(), static_cast<uint32_t>(pointsScratch_.size()));
    }
    return count;
}

void AutomationController::addAutomationPoint(uint64_t frame, float value) {
    auto* trackManager = getTrackManager();
    if (!trackManager || !activeTrackId_.isValid() || activeParameterIndex_ < 0) return;
    auto* manager = trackManager->getAutomationManager(activeTrackId_);
    if (!manager) return;

    composition::AutomationTarget target = resolveActiveTarget();
    std::string paramName;
    if (stringRegistry_) {
        stringRegistry_->getString(target.semanticNameId, paramName);
    }
    bool isBoolean = isParameterBoolean(target.nodeId, target.subNodeId, target.cachedParameterIndex, paramName);
    if (isBoolean) {
        value = (value > 0.5f) ? 1.0f : 0.0f;
    } else {
        // Snap IS_INTEGER parameters to discrete steps (e.g. Panner Mode, MonitorState).
        ::ParameterInfo paramInfo{};
        if (queryPluginParameterInfo(target.nodeId, target.subNodeId, target.cachedParameterIndex, paramInfo)) {
            if ((paramInfo.flags & ::ParameterInfo::IS_INTEGER) != 0) {
                float range = paramInfo.maxValue - paramInfo.minValue;
                if (range > 0.0f) {
                    uint32_t steps = static_cast<uint32_t>(range);
                    float snapped = std::round(value * static_cast<float>(steps)) / static_cast<float>(steps);
                    value = std::clamp(snapped, 0.0f, 1.0f);
                }
            }
        }
    }
    ::AutomationPoint::Shape shape = isBoolean ? ::AutomationPoint::Shape::STEP : ::AutomationPoint::Shape::LINEAR;

    if (recordingGateway_) {
        recordingGateway_->recordValue(
            activeTrackId_, 
            target.nodeId, 
            target.cachedParameterIndex, 
            frame, 
            value, 
            shape
        );
    }
}

void AutomationController::removeAutomationPoint(uint32_t pointIndex) {
    auto* lane = getActiveLane(false); // read-only: enumerate existing points
    if (!lane) return;

    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    auto* manager = trackManager->getAutomationManager(activeTrackId_);
    if (!manager) return;

    // Dynamically retrieve points into scratch buffer without static caps
    uint32_t count = fetchPointsIntoScratch(lane);

    if (pointIndex < count) {
        composition::AutomationTarget target = resolveActiveTarget();
        // manager->removePoint enforces the mode guard (OFF/READ → no-op).
        if (manager->removePoint(target, pointsScratch_[pointIndex].positionSample)) {
            pushUpdatedPointsToProcessor(lane);
        }
    }
}

void AutomationController::setPointShapeAndTension(uint32_t pointIndex, uint8_t shape, float tension) {
    auto* lane = getActiveLane(false);
    if (!lane) return;

    auto* trackManager = getTrackManager();
    if (!trackManager || !activeTrackId_.isValid() || activeParameterIndex_ < 0) return;
    auto* manager = trackManager->getAutomationManager(activeTrackId_);
    if (!manager) return;

    uint32_t count = fetchPointsIntoScratch(lane);
    if (pointIndex < count) {
        composition::AutomationTarget target = resolveActiveTarget();
        auto* managerImpl = dynamic_cast<composition::AutomationLaneManagerImpl*>(manager);
        if (managerImpl) {
            managerImpl->editPointShapeAndTension(activeTrackId_, target.nodeId, target.subNodeId, target.cachedParameterIndex, pointIndex, shape, tension);
            pushUpdatedPointsToProcessor(lane);
        }
    }
}

void AutomationController::editPoints(const uint32_t* indices, uint32_t count, int64_t frameDelta, float valueDelta) {
    auto* lane = getActiveLane(false);
    if (!lane || !indices || count == 0) return;

    // Dynamically retrieve points into scratch buffer without static caps
    uint32_t totalCount = fetchPointsIntoScratch(lane);

    composition::AutomationTarget target = resolveActiveTarget();
    std::string paramName;
    if (stringRegistry_) {
        stringRegistry_->getString(target.semanticNameId, paramName);
    }
    bool isBoolean = isParameterBoolean(target.nodeId, target.subNodeId, target.cachedParameterIndex, paramName);

    struct EditItem {
        uint64_t oldPos;
        uint64_t newPos;
        float newVal;
        ::AutomationPoint::Shape shape;
        float tension;
    };
    std::vector<EditItem> edits;
    edits.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = indices[i];
        if (idx < totalCount) {
            uint64_t newPos = 0;
            if (frameDelta >= 0) {
                newPos = pointsScratch_[idx].positionSample + static_cast<uint64_t>(frameDelta);
            } else {
                uint64_t absDelta = static_cast<uint64_t>(-frameDelta);
                newPos = (pointsScratch_[idx].positionSample > absDelta) ? pointsScratch_[idx].positionSample - absDelta : 0;
            }
            float newVal = std::clamp(pointsScratch_[idx].value + valueDelta, 0.0f, 1.0f);
            ::AutomationPoint::Shape shape = pointsScratch_[idx].curveShape;
            float tension = pointsScratch_[idx].tension;
            if (isBoolean) {
                newVal = (newVal > 0.5f) ? 1.0f : 0.0f;
                shape = ::AutomationPoint::Shape::STEP;
                tension = 0.0f;
            }
            edits.push_back({ pointsScratch_[idx].positionSample, newPos, newVal, shape, tension });
        }
    }

    // Obtain history to wrap the N removes + M adds into a single undo step.
    // If unavailable the edits still proceed; they just produce individual deltas.
    composition::ICommandHistory* history = nullptr;
    if (sessionManager_) {
        if (auto* session = sessionManager_->getActiveSession()) {
            history = session->getCommandHistory();
        }
    }

    auto* editTrackManager = getTrackManager();
    auto* editManager = editTrackManager ? editTrackManager->getAutomationManager(activeTrackId_) : nullptr;

    if (history) history->beginCompound();

    if (editManager) {
        // Remove old points through mode-guarded manager.
        for (const auto& item : edits) {
            editManager->removePoint(target, item.oldPos);
        }

        std::vector<::AutomationPoint> newPoints;
        newPoints.reserve(edits.size());
        for (const auto& item : edits) {
            ::AutomationPoint pt;
            pt.positionSample = item.newPos;
            pt.value = item.newVal;
            pt.curveShape = item.shape;
            pt.tension = item.tension;
            newPoints.push_back(pt);
        }

        if (recordingGateway_) {
            // Use commitBatch with an empty removal range (start > stop) to just insert points
            recordingGateway_->commitBatch(
                activeTrackId_,
                target.nodeId,
                target.cachedParameterIndex,
                newPoints,
                1, 0,
                AutomationMode::WRITE
            );
        }
    }

    if (history) history->endCompound();
}

void AutomationController::clearAutomationLane() {
    auto* lane = getActiveLane(false);
    if (!lane) return;

    // Gather all existing points. Bail early if the lane is already empty.
    uint32_t count = fetchPointsIntoScratch(lane);
    if (count == 0) return;

    // Obtain history for the compound transaction.
    composition::ICommandHistory* history = nullptr;
    if (sessionManager_) {
        if (auto* session = sessionManager_->getActiveSession()) {
            history = session->getCommandHistory();
        }
    }

    // Step 1: Open compound — all removePoint() calls below each push a
    //         REMOVE_POINT delta into this single compound entry.
    if (history) history->beginCompound();

    // Step 2: Emit one REMOVE_POINT delta per point by calling removePoint()
    //         on the lane. This records the old state needed for undo.
    //         We iterate a snapshot (pointsScratch_) since removePoint()
    //         modifies the lane's internal vector.
    for (uint32_t i = 0; i < count; ++i) {
        lane->removePoint(pointsScratch_[i].positionSample);
    }

    // Step 3: Seal the compound — one undo step restores all points.
    if (history) history->endCompound();

    // Step 4: Erase the in-memory vector directly (no further delta is pushed;
    //         the compound above already removed every point individually, so
    //         the vector should already be empty — this is a defensive clear).
    lane->clearPoints();

    // Step 5: Sync the RT automation processor with the now-empty lane.
    pushUpdatedPointsToProcessor(lane);
}

uint32_t AutomationController::copyAutomationPoints(uint64_t startFrame, uint64_t endFrame) {
    clipboardPoints_.clear();
    auto* lane = getActiveLane(false);
    if (!lane) return 0;

    uint32_t count = fetchPointsIntoScratch(lane);
    if (count == 0) return 0;

    for (uint32_t i = 0; i < count; ++i) {
        const auto& pt = pointsScratch_[i];
        if (startFrame == 0 && endFrame == 0) {
            clipboardPoints_.push_back(pt);
        } else if (pt.positionSample >= startFrame && pt.positionSample <= endFrame) {
            clipboardPoints_.push_back(pt);
        }
    }
    return static_cast<uint32_t>(clipboardPoints_.size());
}

void AutomationController::pasteAutomationPoints(uint64_t pasteAtFrame) {
    if (clipboardPoints_.empty()) return;

    auto* lane = getActiveLane(true);
    if (!lane) return;

    uint64_t earliestFrame = clipboardPoints_[0].positionSample;
    for (const auto& pt : clipboardPoints_) {
        if (pt.positionSample < earliestFrame) {
            earliestFrame = pt.positionSample;
        }
    }

    composition::ICommandHistory* history = nullptr;
    if (sessionManager_) {
        if (auto* session = sessionManager_->getActiveSession()) {
            history = session->getCommandHistory();
        }
    }

    if (history) history->beginCompound();

    for (const auto& pt : clipboardPoints_) {
        uint64_t newFrame = pasteAtFrame + (pt.positionSample - earliestFrame);
        lane->addPoint(newFrame, pt.value, pt.curveShape, pt.tension);
    }

    if (history) history->endCompound();

    pushUpdatedPointsToProcessor(lane);
}


void AutomationController::onTransportRecordingStarted() {
    if (!captureEngine_ || !transport_ || !activeTrackId_.isValid() || activeParameterIndex_ < 0) return;

    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    auto* manager = trackManager->getAutomationManager(activeTrackId_);
    if (!manager) return;

    AutomationMode mode = manager->getAutomationMode();
    if (mode == AutomationMode::WRITE ||
        mode == AutomationMode::TOUCH ||
        mode == AutomationMode::LATCH ||
        mode == AutomationMode::TRIM) {
        
        float initialVal = getInitialParameterValue(activeNodeId_, activeTargetSubNodeId_, activeParameterIndex_);
        captureEngine_->startRecording(activeNodeId_, static_cast<uint32_t>(activeParameterIndex_), mode, transport_->getPosition(), initialVal);
    }
}

void AutomationController::onTransportRecordingStopped() {
    stopActiveRecording();
}

void AutomationController::onTransportStateChanged(bool isPlaying) {
    if (touchStateMonitor_) {
        touchStateMonitor_->setTransportState(isPlaying);
    }
}

void AutomationController::setRecorderMode(AutomationMode mode) {
    auto* trackManager = getTrackManager();
    if (!trackManager || !activeTrackId_.isValid()) return;

    auto* manager = trackManager->getAutomationManager(activeTrackId_);
    if (!manager) return;

    manager->setAutomationMode(mode);

    if (touchStateMonitor_) {
        if (auto* managerImpl = dynamic_cast<composition::AutomationLaneManagerImpl*>(manager)) {
            for (const auto& [target, lane] : managerImpl->getLanes()) {
                touchStateMonitor_->setMode(target.nodeId, target.cachedParameterIndex, mode);
            }
        }
        if (activeNodeId_.isValid() && activeParameterIndex_ >= 0) {
            touchStateMonitor_->setMode(activeNodeId_, static_cast<uint32_t>(activeParameterIndex_), mode);
        }
    }
}

void AutomationController::stopActiveRecording() {
    if (!captureEngine_ || !transport_ || !activeNodeId_.isValid() || activeParameterIndex_ < 0) return;
    
    uint64_t stopSample = transport_->getPosition();
    captureEngine_->stopRecording(activeNodeId_, static_cast<uint32_t>(activeParameterIndex_), stopSample);
}

void AutomationController::startTouchRecording() {
    if (!captureEngine_ || !transport_ || !activeTrackId_.isValid() || activeParameterIndex_ < 0) return;

    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    auto* manager = trackManager->getAutomationManager(activeTrackId_);
    if (!manager) return;

    AutomationMode mode = manager->getAutomationMode();
    if (mode == AutomationMode::TOUCH || mode == AutomationMode::LATCH) {
        captureEngine_->touchStarted(activeNodeId_, static_cast<uint32_t>(activeParameterIndex_));
        
        if (transport_->getState() != TransportState::RECORDING) {
            return;
        }

        if (!captureEngine_->isRecording(activeNodeId_, static_cast<uint32_t>(activeParameterIndex_))) {
            float initialVal = getInitialParameterValue(activeNodeId_, activeTargetSubNodeId_, activeParameterIndex_);
            captureEngine_->startRecording(activeNodeId_, static_cast<uint32_t>(activeParameterIndex_), mode, transport_->getPosition(), initialVal);
        }
    }
}

void AutomationController::stopTouchRecording() {
    if (!captureEngine_ || !activeTrackId_.isValid() || activeParameterIndex_ < 0) return;

    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    auto* manager = trackManager->getAutomationManager(activeTrackId_);
    if (!manager) return;

    AutomationMode mode = manager->getAutomationMode();
    if (mode == AutomationMode::TOUCH) {
        captureEngine_->touchStopped(activeNodeId_, static_cast<uint32_t>(activeParameterIndex_));
        stopActiveRecording();
    }
}

void AutomationController::recordValue(float value) {
    if (!captureEngine_ || !transport_ || !activeTrackId_.isValid() || activeParameterIndex_ < 0) return;

    if (captureEngine_->isRecording(activeNodeId_, static_cast<uint32_t>(activeParameterIndex_))) {
        CapturePoint cp{};
        cp.targetId = activeNodeId_;
        cp.parameterIndex = static_cast<uint32_t>(activeParameterIndex_);
        cp.timestamp = transport_->getPosition();
        cp.value = value;
        cp.flags = CapturePoint::FLAG_TOUCH;
        captureEngine_->getCaptureQueue().push(cp);
        captureEngine_->process();
    }
}

void AutomationController::compileAndPushPoints(const composition::AutomationTarget& target, composition::IAutomationLane* lane) {
    if (!lane || !processor_) return;

    // Retrieve all points from lane
    uint32_t count = fetchPointsIntoScratch(lane);

    processor_->updatePlaybackPoints(target.nodeId, target.subNodeId, target.cachedParameterIndex, pointsScratch_.data(), count);
}

void AutomationController::pushUpdatedPointsToProcessor(composition::IAutomationLane* lane) {
    if (!lane || !activeTrackId_.isValid()) return;
    
    composition::AutomationTarget target = resolveActiveTarget();
    compileAndPushPoints(target, lane);
}

float AutomationController::getBaseParameterValue(NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex) const {
    return getInitialParameterValue(targetNodeId, subNodeId, static_cast<int32_t>(parameterIndex));
}

uint32_t AutomationController::getCurvePoints(
    TrackID trackId,
    NodeID targetNodeId,
    uint32_t subNodeId,
    uint32_t parameterIndex,
    uint64_t startFrame,
    uint64_t endFrame,
    VisualAutomationPoint* outPoints,
    uint32_t maxPoints
) const {
    auto* trackManager = getTrackManager();
    if (!trackManager || !outPoints || maxPoints == 0) return 0;

    auto* manager = trackManager->getAutomationManager(trackId);
    if (!manager) return 0;

    auto* managerImpl = dynamic_cast<composition::AutomationLaneManagerImpl*>(manager);
    if (!managerImpl) return 0;

    composition::IAutomationLane* lane = nullptr;
    for (const auto& [target, targetLane] : managerImpl->getLanes()) {
        if (target.nodeId == targetNodeId && target.subNodeId == subNodeId && target.cachedParameterIndex == parameterIndex) {
            lane = targetLane.get();
            break;
        }
    }

    if (!lane) return 0;

    uint32_t count = fetchPointsIntoScratch(lane);

    uint32_t visibleCount = 0;
    // Find the first point that is >= startFrame
    auto itStart = std::lower_bound(pointsScratch_.begin(), pointsScratch_.begin() + count, startFrame,
        [](const composition::Point& pt, uint64_t val) {
            return pt.positionSample < val;
        });

    // If there is a point before startFrame, include it so the curve is drawn from outside the left edge
    if (itStart != pointsScratch_.begin()) {
        auto prevIt = std::prev(itStart);
        outPoints[visibleCount].pointIndex = static_cast<uint32_t>(std::distance(pointsScratch_.begin(), prevIt));
        outPoints[visibleCount].framePosition = prevIt->positionSample;
        outPoints[visibleCount].normalizedValue = prevIt->value;
        outPoints[visibleCount].isSelected = false;
        outPoints[visibleCount].curveShape = static_cast<uint8_t>(prevIt->curveShape);
        outPoints[visibleCount].tension = prevIt->tension;
        visibleCount++;
    }

    // Now include all points inside the window
    auto it = itStart;
    while (it != pointsScratch_.begin() + count && it->positionSample <= endFrame) {
        if (visibleCount < maxPoints) {
            outPoints[visibleCount].pointIndex = static_cast<uint32_t>(std::distance(pointsScratch_.begin(), it));
            outPoints[visibleCount].framePosition = it->positionSample;
            outPoints[visibleCount].normalizedValue = it->value;
            outPoints[visibleCount].isSelected = false;
            outPoints[visibleCount].curveShape = static_cast<uint8_t>(it->curveShape);
            outPoints[visibleCount].tension = it->tension;
            visibleCount++;
        }
        ++it;
    }

    // If there is a point after endFrame, include it so the curve continues to the right edge
    if (it != pointsScratch_.begin() + count) {
        if (visibleCount < maxPoints) {
            outPoints[visibleCount].pointIndex = static_cast<uint32_t>(std::distance(pointsScratch_.begin(), it));
            outPoints[visibleCount].framePosition = it->positionSample;
            outPoints[visibleCount].normalizedValue = it->value;
            outPoints[visibleCount].isSelected = false;
            outPoints[visibleCount].curveShape = static_cast<uint8_t>(it->curveShape);
            outPoints[visibleCount].tension = it->tension;
            visibleCount++;
        }
    }

    return visibleCount;
}

composition::IAutomationLane* AutomationController::getActiveLane(bool createIfMissing) const {
    auto* trackManager = getTrackManager();
    if (!trackManager || !stringRegistry_ || !activeTrackId_.isValid() || activeParameterIndex_ < 0) {
        return nullptr;
    }

    auto* manager = trackManager->getAutomationManager(activeTrackId_);
    if (!manager) return nullptr;

    composition::AutomationTarget target = resolveActiveTarget();
    
    if (createIfMissing) {
        return manager->createLane(target);
    }
    return manager->getLane(target);
}

bool AutomationController::isAutomationVisible(TrackID trackId) const {
    auto* trackManager = getTrackManager();
    if (!trackManager) return false;
    auto* manager = trackManager->getAutomationManager(trackId);
    return manager->getAutomationMode() != AutomationMode::OFF;
}

bool AutomationController::isAutomationWriteEnabled(TrackID trackId) const {
    auto* trackManager = getTrackManager();
    if (!trackManager) return false;
    auto* manager = trackManager->getAutomationManager(trackId);
    if (!manager) return false;
    auto mode = manager->getAutomationMode();
    bool enabled = (mode == AutomationMode::WRITE ||
                    mode == AutomationMode::TOUCH ||
                    mode == AutomationMode::LATCH ||
                    mode == AutomationMode::TRIM);
    if (!enabled) return false;

    if (activeTrackId_ == trackId && activeNodeId_.isValid() && activeParameterIndex_ >= 0) {
        ::ParameterInfo paramInfo{};
        if (queryPluginParameterInfo(activeNodeId_, activeTargetSubNodeId_, static_cast<uint32_t>(activeParameterIndex_), paramInfo)) {
            if ((paramInfo.flags & ::ParameterInfo::IS_READ_ONLY) != 0) {
                return false;
            }
        }
    }
    return true;
}

void AutomationController::createAutomationLane(TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, uint32_t parameterIndex) {
    auto* trackManager = getTrackManager();
    if (!trackManager || !stringRegistry_) return;

    auto* manager = trackManager->getAutomationManager(trackId);
    if (!manager) return;

    // Resolve the human-readable parameter name using the same helper as
    // eagerlyCreateStandardAutomationLanes() — covers all node types.
    std::string name = queryPluginParameterName(trackId, routingNodeId, subNodeId, parameterIndex);
    if (name.empty()) {
        name = "Param_" + std::to_string(parameterIndex);
    }

    uint32_t nameId = stringRegistry_->registerString(name);
    composition::AutomationTarget target{ routingNodeId, nameId, parameterIndex, subNodeId };

    // createLane() is idempotent: returns the existing lane if the target already exists.
    manager->createLane(target, true);
}

void AutomationController::removeAutomationLane(TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, uint32_t parameterIndex) {
    auto* trackManager = getTrackManager();
    if (!trackManager || !stringRegistry_) return;

    auto* manager = trackManager->getAutomationManager(trackId);
    if (!manager) return;

    std::string name = queryPluginParameterName(trackId, routingNodeId, subNodeId, parameterIndex);
    if (name.empty()) {
        name = "Param_" + std::to_string(parameterIndex);
    }

    uint32_t nameId = stringRegistry_->registerString(name);
    composition::AutomationTarget target{ routingNodeId, nameId, parameterIndex, subNodeId };

    // Save current active lane to restore later
    TrackID prevTrackId = activeTrackId_;
    NodeID prevNodeId = activeNodeId_;
    uint32_t prevSubNodeId = activeTargetSubNodeId_;
    int32_t prevParamIdx = activeParameterIndex_;

    composition::ICommandHistory* history = nullptr;
    if (sessionManager_) {
        if (auto* session = sessionManager_->getActiveSession()) {
            history = session->getCommandHistory();
        }
    }

    if (history) history->beginCompound();

    // Set the lane as active so we can clear it and get undo history
    selectActiveAutomationLane(trackId, routingNodeId, subNodeId, static_cast<int32_t>(parameterIndex));
    clearAutomationLane(); // this generates the undo history for point removals
    
    // Now physically remove the lane
    manager->removeLane(target, true);

    if (history) history->endCompound();

    // Restore previous active lane
    selectActiveAutomationLane(prevTrackId, prevNodeId, prevSubNodeId, prevParamIdx);

    // Also we must push updated points to processor (an empty list)
    if (processor_) {
        processor_->updatePlaybackPoints(routingNodeId, subNodeId, parameterIndex, nullptr, 0);
    }
}

} // namespace bridge
