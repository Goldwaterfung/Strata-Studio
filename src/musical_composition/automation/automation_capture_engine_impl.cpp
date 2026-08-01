#include "automation_capture_engine_impl.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/command_history/icommand_history.h"
#include "Core audio engine/automation/iautomation_processor.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "common/dsp/automation_fsm.h"
#include "common/dsp/curve_interpolation.h"
#include "musical_composition/automation/iautomation_lane_manager.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/plugins/insert_plugin_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "Core audio engine/plugin/iplugin.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/monitor_switch/monitor_switch_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include <cmath>
#include <algorithm>
#include <map>

namespace composition {

namespace {

bool isParameterBoolean(NodeID nodeId, uint32_t subNodeId, uint32_t parameterIndex, const std::string& name) {
    if (name == "Mute" || name == "Solo" || name == "Bypass" || name == "bypass" || name == "mute" || name == "solo") {
        return true;
    }
    if (!nodeId.isValid()) return false;
    
    if (subNodeId != 0) {
        if (auto* slotNode = DSP::PluginSlotFactory::getRegistry().get(nodeId)) {
            for (uint32_t i = 0; i < DSP::PluginSlotState::MAX_SLOTS; ++i) {
                if (slotNode->slots[i].isValid() && slotNode->slots[i].id == subNodeId) {
                    if (auto* ip = DSP::InsertPluginFactory::getRegistry().get(slotNode->slots[i])) {
                        if (parameterIndex == ::BYPASS_PARAMETER_INDEX) {
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
                                ::ParameterInfo paramInfo{};
                                if (plugin->getParameterInfo(p, paramInfo)) {
                                    if (paramInfo.index == parameterIndex) {
                                        if ((paramInfo.flags & ::ParameterInfo::IS_BOOLEAN) != 0) {
                                            return true;
                                        }
                                        break;
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
        if (parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Mute) ||
            parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Solo)) {
            return true;
        }
    }
    
    if (auto* ip = DSP::InsertPluginFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == ::BYPASS_PARAMETER_INDEX) {
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
                ::ParameterInfo paramInfo{};
                if (plugin->getParameterInfo(p, paramInfo)) {
                    if (paramInfo.index == parameterIndex) {
                        if ((paramInfo.flags & ::ParameterInfo::IS_BOOLEAN) != 0) {
                            return true;
                        }
                        break;
                    }
                }
            }
        }
    }
    
    if (auto* inst = DSP::getInstrumentSlotState(nodeId)) {
        if (parameterIndex == ::BYPASS_PARAMETER_INDEX) {
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
                ::ParameterInfo paramInfo{};
                if (plugin->getParameterInfo(p, paramInfo)) {
                    if (paramInfo.index == parameterIndex) {
                        if ((paramInfo.flags & ::ParameterInfo::IS_BOOLEAN) != 0) {
                            return true;
                        }
                        break;
                    }
                }
            }
        }
    }
    return false;
}



std::string queryPluginParameterName(NodeID nodeId, uint32_t subNodeId, uint32_t parameterIndex) {
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
                                    if (paramInfo.index == parameterIndex) {
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
                    if (paramInfo.index == parameterIndex) {
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
                    if (paramInfo.index == parameterIndex) {
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

    if (DSP::SendFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == 0) return "SendGain";
        return "";
    }

    if (DSP::MonitorSwitchFactory::getRegistry().get(nodeId)) {
        if (parameterIndex == 0) return "MonitorState";
        return "";
    }

    // Fallback for mock/unregistered nodes in unit tests
    if (parameterIndex == 0) return "Volume";
    if (parameterIndex == 1) return "Pan";
    if (parameterIndex == 2) return "Mute";
    if (parameterIndex == 3) return "Solo";

    return "";
}

void rdpSimplifyRecursive(const std::vector<::AutomationPoint>& points, size_t start, size_t end, float epsilon, std::vector<bool>& keep) {
    if (end - start <= 1) return;

    float maxDistance = 0.0f;
    size_t index = start;

    uint64_t x1 = points[start].positionSample;
    float y1 = points[start].value;
    uint64_t x2 = points[end].positionSample;
    float y2 = points[end].value;

    for (size_t i = start + 1; i < end; ++i) {
        uint64_t xi = points[i].positionSample;
        float yi = points[i].value;

        float yi_interp = y1;
        if (x2 > x1) {
            yi_interp = y1 + static_cast<float>(xi - x1) / static_cast<float>(x2 - x1) * (y2 - y1);
        }
        float distance = std::abs(yi - yi_interp);

        if (distance > maxDistance) {
            maxDistance = distance;
            index = i;
        }
    }

    if (maxDistance > epsilon) {
        keep[index] = true;
        rdpSimplifyRecursive(points, start, index, epsilon, keep);
        rdpSimplifyRecursive(points, index, end, epsilon, keep);
    }
}

std::vector<::AutomationPoint> simplifyPoints(const std::vector<::AutomationPoint>& points, float epsilon) {
    if (points.size() <= 2) return points;
    std::vector<bool> keep(points.size(), false);
    keep.front() = true;
    keep.back() = true;
    rdpSimplifyRecursive(points, 0, points.size() - 1, epsilon, keep);

    std::vector<::AutomationPoint> result;
    result.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        if (keep[i]) {
            result.push_back(points[i]);
        }
    }
    return result;
}

} // namespace

AutomationCaptureEngineImpl::AutomationCaptureEngineImpl(
    Layer2::IStringRegistry* stringRegistry,
    Layer3::IAutomationProcessor* processor,
    DSP::ITouchStateMonitor* touchStateMonitor,
    bridge::IAutomationRecordingGateway* recordingGateway
) : stringRegistry_(stringRegistry),
    processor_(processor),
    touchStateMonitor_(touchStateMonitor),
    recordingGateway_(recordingGateway) {}

void AutomationCaptureEngineImpl::setActiveSession(
    ITrackManager* trackManager,
    ICommandHistory* commandHistory
) {
    std::unique_lock lock(mutex_);
    trackManager_ = trackManager;
    commandHistory_ = commandHistory;
}

void AutomationCaptureEngineImpl::startRecording(
    NodeID targetId,
    uint32_t parameterIndex,
    AutomationMode mode,
    uint64_t startSample,
    float initialValue
) {
    std::unique_lock lock(mutex_);
    uint64_t key = makeKey(targetId, parameterIndex);
    auto& session = sessions_[key];
    
    session.mode = mode;
    session.isRecording = true;
    session.startSample = startSample;
    session.points.clear();
    session.lastTimestamp = 0;
    session.lastValue = initialValue;
    session.preTouchValue = initialValue;
    session.lastWasRedundant = false;

    if (touchStateMonitor_) {
        touchStateMonitor_->setRecording(targetId, parameterIndex, true);
    }
}

void AutomationCaptureEngineImpl::stopRecording(
    NodeID targetId,
    uint32_t parameterIndex,
    uint64_t stopSample
) {
    std::unique_lock lock(mutex_);
    uint64_t key = makeKey(targetId, parameterIndex);
    auto it = sessions_.find(key);
    if (it == sessions_.end() || !it->second.isRecording) {
        return;
    }

    auto& session = it->second;
    finalizeSession(session, stopSample, targetId, parameterIndex);

    if (touchStateMonitor_) {
        touchStateMonitor_->setRecording(targetId, parameterIndex, false);
    }

    if (session.points.empty()) {
        sessions_.erase(it);
        return;
    }

    // Resolve track and lane manager
    TrackID trackId = resolveTrackForNode(targetId);
    if (!trackId.isValid() || !trackManager_) {
        sessions_.erase(it);
        return;
    }

    auto* laneManager = trackManager_->getAutomationManager(trackId);
    if (!laneManager) {
        sessions_.erase(it);
        return;
    }

    AutomationTarget target = resolveActiveTarget(targetId, parameterIndex);
    auto* lane = laneManager->createLane(target);
    if (!lane) {
        sessions_.erase(it);
        return;
    }

    // Fetch existing points
    std::vector<::AutomationPoint> existingPoints(4096);
    uint32_t count = lane->getPoints(existingPoints.data(), static_cast<uint32_t>(existingPoints.size()));
    while (count == existingPoints.size()) {
        existingPoints.resize(existingPoints.size() * 2);
        count = lane->getPoints(existingPoints.data(), static_cast<uint32_t>(existingPoints.size()));
    }
    existingPoints.resize(count);

    uint64_t startSample = session.startSample;
    AutomationMode mode = session.mode;
    std::vector<::AutomationPoint> recordedPoints = simplifyPoints(session.points, 0.001f);

    // Begin history transaction if history is available
    if (commandHistory_) {
        commandHistory_->beginCompound();
    }

    // Commit logic
    if (mode == AutomationMode::WRITE) {
        if (recordingGateway_) {
            recordingGateway_->commitBatch(trackId, targetId, parameterIndex, recordedPoints, startSample, stopSample, mode);
        } else {
            for (const auto& pt : existingPoints) {
                if (pt.positionSample >= startSample && pt.positionSample <= stopSample) {
                    lane->removePoint(pt.positionSample);
                }
            }
            for (const auto& pt : recordedPoints) {
                lane->addPoint(pt.positionSample, pt.value, pt.curveShape, pt.tension);
            }
        }
    }
    else if (mode == AutomationMode::TOUCH || mode == AutomationMode::LATCH) {
        if (!recordedPoints.empty()) {
            uint64_t rangeStart = recordedPoints.front().positionSample;
            uint64_t rangeEnd   = recordedPoints.back().positionSample;
            if (recordingGateway_) {
                recordingGateway_->commitBatch(trackId, targetId, parameterIndex, recordedPoints, rangeStart, rangeEnd, mode);
            } else {
                for (const auto& pt : existingPoints) {
                    if (pt.positionSample >= rangeStart && pt.positionSample <= rangeEnd) {
                        lane->removePoint(pt.positionSample);
                    }
                }
                for (const auto& pt : recordedPoints) {
                    lane->addPoint(pt.positionSample, pt.value, pt.curveShape, pt.tension);
                }
            }
        }
    }
    else if (mode == AutomationMode::TRIM) {
        if (!recordedPoints.empty()) {
            uint64_t rangeStart = recordedPoints.front().positionSample;
            uint64_t rangeEnd   = recordedPoints.back().positionSample;

            float preTouchValue = session.preTouchValue;
            std::map<uint64_t, ::AutomationPoint> merged;

            auto getRecordedValueAtLocal = [&](uint64_t ts) -> float {
                if (recordedPoints.empty()) return preTouchValue;
                if (ts <= recordedPoints.front().positionSample) return recordedPoints.front().value;
                if (ts >= recordedPoints.back().positionSample) return recordedPoints.back().value;
                for (size_t i = 0; i < recordedPoints.size() - 1; ++i) {
                    if (ts >= recordedPoints[i].positionSample && ts <= recordedPoints[i+1].positionSample) {
                        return DSP::CurveInterpolator::calculate(recordedPoints[i], recordedPoints[i+1], ts);
                    }
                }
                return preTouchValue;
            };

            auto getExistingValueAtLocal = [&](uint64_t ts) -> float {
                if (existingPoints.empty()) return 0.0f;
                if (ts <= existingPoints.front().positionSample) return existingPoints.front().value;
                if (ts >= existingPoints.back().positionSample) return existingPoints.back().value;
                for (size_t i = 0; i < existingPoints.size() - 1; ++i) {
                    if (ts >= existingPoints[i].positionSample && ts <= existingPoints[i+1].positionSample) {
                        return DSP::CurveInterpolator::calculate(existingPoints[i], existingPoints[i+1], ts);
                    }
                }
                return 0.0f;
            };

            std::string paramName;
            if (stringRegistry_) {
                stringRegistry_->getString(target.semanticNameId, paramName);
            }
            bool isBoolean = isParameterBoolean(targetId, 0, parameterIndex, paramName);

            for (const auto& pt : existingPoints) {
                if (pt.positionSample >= rangeStart && pt.positionSample <= rangeEnd) {
                    float delta = getRecordedValueAtLocal(pt.positionSample) - preTouchValue;
                    float newVal = std::clamp(pt.value + delta, 0.0f, 1.0f);
                    if (isBoolean) {
                        newVal = (newVal > 0.5f) ? 1.0f : 0.0f;
                    }
                    ::AutomationPoint::Shape shape = isBoolean ? ::AutomationPoint::Shape::STEP : pt.curveShape;
                    merged[pt.positionSample] = { pt.positionSample, newVal, shape, isBoolean ? 0.0f : pt.tension };
                }
            }
            for (const auto& pt : recordedPoints) {
                float existingVal = getExistingValueAtLocal(pt.positionSample);
                float delta = pt.value - preTouchValue;
                float newVal = std::clamp(existingVal + delta, 0.0f, 1.0f);
                if (isBoolean) {
                    newVal = (newVal > 0.5f) ? 1.0f : 0.0f;
                }
                ::AutomationPoint::Shape shape = isBoolean ? ::AutomationPoint::Shape::STEP : pt.curveShape;
                merged[pt.positionSample] = { pt.positionSample, newVal, shape, isBoolean ? 0.0f : pt.tension };
            }
            std::vector<::AutomationPoint> finalPoints;
            for (const auto& [pos, mergedPt] : merged) {
                finalPoints.push_back(mergedPt);
            }

            if (recordingGateway_) {
                recordingGateway_->commitBatch(trackId, targetId, parameterIndex, finalPoints, rangeStart, rangeEnd, mode);
            } else {
                for (const auto& pt : existingPoints) {
                    if (pt.positionSample >= rangeStart && pt.positionSample <= rangeEnd) {
                        lane->removePoint(pt.positionSample);
                    }
                }
                for (const auto& pt : finalPoints) {
                    lane->addPoint(pt.positionSample, pt.value, pt.curveShape, pt.tension);
                }
            }
        }
    }

    if (commandHistory_) {
        commandHistory_->endCompound();
    }

    if (!recordingGateway_) {
        pushUpdatedPointsToProcessor(lane, targetId, parameterIndex);
    }

    sessions_.erase(it);
}

void AutomationCaptureEngineImpl::abortRecording(NodeID targetId, uint32_t parameterIndex) {
    std::unique_lock lock(mutex_);
    uint64_t key = makeKey(targetId, parameterIndex);
    sessions_.erase(key);

    if (touchStateMonitor_) {
        touchStateMonitor_->setRecording(targetId, parameterIndex, false);
    }
}

void AutomationCaptureEngineImpl::touchStarted(NodeID targetId, uint32_t parameterIndex) {
    if (touchStateMonitor_) {
        touchStateMonitor_->setTouching(targetId, parameterIndex, true);
    }
}

void AutomationCaptureEngineImpl::touchStopped(NodeID targetId, uint32_t parameterIndex) {
    if (touchStateMonitor_) {
        touchStateMonitor_->setTouching(targetId, parameterIndex, false);
    }
}

bool AutomationCaptureEngineImpl::isRecording(NodeID targetId, uint32_t parameterIndex) const {
    std::unique_lock lock(mutex_);
    uint64_t key = makeKey(targetId, parameterIndex);
    auto it = sessions_.find(key);
    return (it != sessions_.end() && it->second.isRecording);
}

AutomationMode AutomationCaptureEngineImpl::getMode(NodeID targetId, uint32_t parameterIndex) const {
    std::unique_lock lock(mutex_);
    uint64_t key = makeKey(targetId, parameterIndex);
    auto it = sessions_.find(key);
    return (it != sessions_.end()) ? it->second.mode : AutomationMode::OFF;
}

RecorderState AutomationCaptureEngineImpl::getState(NodeID targetId, uint32_t parameterIndex) const {
    std::unique_lock lock(mutex_);
    uint64_t key = makeKey(targetId, parameterIndex);
    auto it = sessions_.find(key);
    if (it != sessions_.end()) {
        const auto& s = it->second;
        return {targetId, parameterIndex, s.mode, s.isRecording, s.startSample};
    }
    return {targetId, parameterIndex, AutomationMode::OFF, false, 0};
}

void AutomationCaptureEngineImpl::process() {
    CapturePoint cp;
    while (queue_.pop(cp)) {
        std::unique_lock lock(mutex_);
        uint64_t key = makeKey(cp.targetId, cp.parameterIndex);
        
        auto it = sessions_.find(key);
        if (it == sessions_.end() || !it->second.isRecording) {
            continue;
        }

        auto& session = it->second;
        
        // Capture pre-touch value on the very first point or if FLAG_TOUCH is set
        if (session.points.empty() || (cp.flags & CapturePoint::FLAG_TOUCH)) {
            float val = cp.value;
            std::string paramName;
            if (stringRegistry_) {
                AutomationTarget target = resolveActiveTarget(cp.targetId, cp.parameterIndex);
                stringRegistry_->getString(target.semanticNameId, paramName);
            }
            bool isBoolean = isParameterBoolean(cp.targetId, 0, cp.parameterIndex, paramName);
            if (isBoolean) {
                val = (val > 0.5f) ? 1.0f : 0.0f;
            }
            session.preTouchValue = val;
            // Anchor the start of the recording at startSample (only for WRITE mode)
            if (session.points.empty() && session.mode == AutomationMode::WRITE && session.startSample < cp.timestamp) {
                ::AutomationPoint::Shape shape = isBoolean ? ::AutomationPoint::Shape::STEP : ::AutomationPoint::Shape::LINEAR;
                session.points.push_back({session.startSample, val, shape, 0.0f});
            }
        }

        emitPoint(session, cp.timestamp, cp.value, cp.targetId, cp.parameterIndex);
    }
}

void AutomationCaptureEngineImpl::thinData(NodeID targetId, uint32_t parameterIndex, float tolerance) {
    std::unique_lock lock(mutex_);
    uint64_t key = makeKey(targetId, parameterIndex);
    auto it = sessions_.find(key);
    if (it == sessions_.end()) return;
    
    auto& session = it->second;
    session.thinningTolerance = tolerance;
    
    if (session.points.empty()) return;

    std::vector<::AutomationPoint> thinned;
    thinned.push_back(session.points[0]);
    
    for (size_t i = 1; i < session.points.size(); ++i) {
        float diff = std::abs(session.points[i].value - thinned.back().value);
        if (diff >= tolerance || i == session.points.size() - 1) {
            thinned.push_back(session.points[i]);
        }
    }
    session.points = std::move(thinned);
}

void AutomationCaptureEngineImpl::smoothData(NodeID targetId, uint32_t parameterIndex, uint32_t windowSize) {
    std::unique_lock lock(mutex_);
    uint64_t key = makeKey(targetId, parameterIndex);
    auto it = sessions_.find(key);
    if (it == sessions_.end() || it->second.points.size() < 2) return;
    
    auto& session = it->second;
    if (windowSize < 2) return;

    std::string paramName;
    if (stringRegistry_) {
        AutomationTarget target = resolveActiveTarget(targetId, parameterIndex);
        stringRegistry_->getString(target.semanticNameId, paramName);
    }
    if (isParameterBoolean(targetId, 0, parameterIndex, paramName)) {
        return; // Do not smooth boolean data
    }

    std::vector<::AutomationPoint> smoothed = session.points;
    const int32_t halfWindow = static_cast<int32_t>(windowSize) / 2;

    for (size_t i = 0; i < session.points.size(); ++i) {
        float sum = 0.0f;
        uint32_t count = 0;
        
        for (int32_t j = -halfWindow; j <= halfWindow; ++j) {
            const int64_t idx = static_cast<int64_t>(i) + j;
            if (idx >= 0 && idx < static_cast<int64_t>(session.points.size())) {
                sum += session.points[static_cast<size_t>(idx)].value;
                count++;
            }
        }
        
        if (count > 0) {
            smoothed[i].value = sum / static_cast<float>(count);
        }
    }
    session.points = std::move(smoothed);
}

void AutomationCaptureEngineImpl::emitPoint(ParameterSession& session, uint64_t timestamp, float value, NodeID targetId, uint32_t parameterIndex) {
    std::string paramName;
    if (stringRegistry_) {
        AutomationTarget target = resolveActiveTarget(targetId, parameterIndex);
        stringRegistry_->getString(target.semanticNameId, paramName);
    }
    bool isBoolean = isParameterBoolean(targetId, 0, parameterIndex, paramName);
    if (isBoolean) {
        value = (value > 0.5f) ? 1.0f : 0.0f;
    }

    bool isFirstPoint = session.points.empty();
    
    // Thinning check
    bool isRedundant = !isFirstPoint && std::abs(value - session.lastValue) < session.thinningTolerance;

    if (isRedundant) {
        session.lastWasRedundant = true;
        session.lastTimestamp = timestamp;
        session.lastValue = value;
        return;
    }

    ::AutomationPoint::Shape shape = isBoolean ? ::AutomationPoint::Shape::STEP : ::AutomationPoint::Shape::LINEAR;

    if (session.lastWasRedundant) {
        session.points.push_back({session.lastTimestamp, session.lastValue, shape, 0.0f});
    }

    session.points.push_back({timestamp, value, shape, 0.0f});
    session.lastValue = value;
    session.lastTimestamp = timestamp;
    session.lastWasRedundant = false;
}

void AutomationCaptureEngineImpl::finalizeSession(ParameterSession& session, uint64_t stopSample, NodeID targetId, uint32_t parameterIndex) {
    if (!session.isRecording) return;

    if (session.points.empty()) {
        session.isRecording = false;
        return;
    }

    std::string paramName;
    if (stringRegistry_) {
        AutomationTarget target = resolveActiveTarget(targetId, parameterIndex);
        stringRegistry_->getString(target.semanticNameId, paramName);
    }
    bool isBoolean = isParameterBoolean(targetId, 0, parameterIndex, paramName);
    ::AutomationPoint::Shape shape = isBoolean ? ::AutomationPoint::Shape::STEP : ::AutomationPoint::Shape::LINEAR;

    if (session.lastWasRedundant) {
        session.points.push_back({session.lastTimestamp, session.lastValue, shape, 0.0f});
    }

    if (session.mode == AutomationMode::TOUCH) {
        float val = session.preTouchValue;
        if (isBoolean) {
            val = (val > 0.5f) ? 1.0f : 0.0f;
        }
        session.points.push_back({stopSample, val, shape, 0.0f});
    } else {
        if (session.points.empty() || session.points.back().positionSample < stopSample) {
            float val = session.points.empty() ? session.preTouchValue : session.lastValue;
            if (isBoolean) {
                val = (val > 0.5f) ? 1.0f : 0.0f;
            }
            session.points.push_back({stopSample, val, shape, 0.0f});
        }
    }

    session.isRecording = false;
}

TrackID AutomationCaptureEngineImpl::resolveTrackForNode(NodeID nodeId) const {
    if (!trackManager_ || !nodeId.isValid()) return TrackID::invalid();
    
    for (const auto& trackId : trackManager_->getAllTrackIDs()) {
        auto desc = trackManager_->getPipelineDescriptor(trackId);
        if (desc.trackNode == nodeId ||
            desc.sourceNode == nodeId ||
            desc.instrumentSlotNode == nodeId ||
            desc.audioInputNode == nodeId) {
            return trackId;
        }
    }
    return TrackID::invalid();
}

AutomationTarget AutomationCaptureEngineImpl::resolveActiveTarget(NodeID targetId, uint32_t parameterIndex) const {
    uint32_t semanticNameId = 0;
    if (stringRegistry_) {
        std::string realName = queryPluginParameterName(targetId, 0, parameterIndex);
        if (realName.empty()) {
            realName = "Param_" + std::to_string(parameterIndex);
        }
        semanticNameId = stringRegistry_->registerString(realName);
    }
    return { targetId, semanticNameId, parameterIndex };
}

void AutomationCaptureEngineImpl::pushUpdatedPointsToProcessor(IAutomationLane* lane, NodeID targetId, uint32_t parameterIndex) {
    if (!lane || !processor_) return;

    std::vector<::AutomationPoint> scratch(4096);
    uint32_t count = lane->getPoints(scratch.data(), static_cast<uint32_t>(scratch.size()));
    while (count == scratch.size()) {
        scratch.resize(scratch.size() * 2);
        count = lane->getPoints(scratch.data(), static_cast<uint32_t>(scratch.size()));
    }
    scratch.resize(count);

    processor_->updatePlaybackPoints(targetId, lane->getTarget().subNodeId, parameterIndex, scratch.data(), count);
}

std::unique_ptr<IAutomationCaptureEngine> IAutomationCaptureEngine::create(
    Layer2::IStringRegistry* stringRegistry,
    Layer3::IAutomationProcessor* processor,
    DSP::ITouchStateMonitor* touchStateMonitor,
    bridge::IAutomationRecordingGateway* recordingGateway
) {
    return std::make_unique<AutomationCaptureEngineImpl>(stringRegistry, processor, touchStateMonitor, recordingGateway);
}

} // namespace composition
