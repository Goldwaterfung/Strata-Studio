#include "automation/iautomation_recording_gateway.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/automation/iautomation_lane_manager.h"
#include "musical_composition/automation/iautomation_lane.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Core audio engine/automation/iautomation_processor.h"
#include "project/isession_manager.h"

// Factory includes for queryParameterName
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/monitor_switch/monitor_switch_node.h"
#include "DSP nodes/plugins/insert_plugin_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "Core audio engine/plugin/iplugin.h"


namespace bridge {

class AutomationRecordingGateway : public IAutomationRecordingGateway {
public:
    AutomationRecordingGateway(
        ISessionManager* sessionManager,
        Layer2::IStringRegistry* stringRegistry,
        Layer3::IAutomationProcessor* processor
    ) : sessionManager_(sessionManager)
      , stringRegistry_(stringRegistry)
      , processor_(processor) {}

    void recordValue(
        TrackID trackId,
        NodeID targetNodeId,
        uint32_t parameterIndex,
        uint64_t samplePosition,
        float value,
        ::AutomationPoint::Shape shape
    ) override {
        if (!sessionManager_ || !stringRegistry_ || !processor_) return;

        auto* session = sessionManager_->getActiveSession();
        if (!session) return;
        auto* trackManager = session->getTrackManager();
        if (!trackManager) return;

        auto* manager = trackManager->getAutomationManager(trackId);
        if (!manager) return;

        std::string name = queryParameterName(targetNodeId, parameterIndex);
        uint32_t nameId = stringRegistry_->registerString(name.empty()
            ? "Param_" + std::to_string(parameterIndex)
            : name);

        composition::AutomationTarget target{ targetNodeId, nameId, parameterIndex };

        // manager->addPoint() enforces the mode guard (OFF/READ → returns false).
        if (!manager->addPoint(target, samplePosition, value, shape, 0.0f)) {
            return;
        }

        // Sync to RT processor
        auto* lane = manager->getLane(target);
        if (lane) {
            pushToProcessor(lane, targetNodeId, parameterIndex);
        }
    }

    void commitBatch(
        TrackID trackId,
        NodeID targetNodeId,
        uint32_t parameterIndex,
        const std::vector<::AutomationPoint>& points,
        uint64_t startSample,
        uint64_t stopSample,
        [[maybe_unused]] AutomationMode mode
    ) override {
        if (!sessionManager_ || !stringRegistry_ || !processor_) return;

        auto* session = sessionManager_->getActiveSession();
        if (!session) return;
        auto* trackManager = session->getTrackManager();
        if (!trackManager) return;

        auto* manager = trackManager->getAutomationManager(trackId);
        if (!manager) return;

        std::string name = queryParameterName(targetNodeId, parameterIndex);
        uint32_t nameId = stringRegistry_->registerString(name.empty()
            ? "Param_" + std::to_string(parameterIndex)
            : name);

        composition::AutomationTarget target{ targetNodeId, nameId, parameterIndex };
        auto* lane = manager->createLane(target);
        if (!lane) return;

        // Fetch existing points
        std::vector<::AutomationPoint> existing;
        {
            std::vector<::AutomationPoint> scratch(4096);
            uint32_t cnt = lane->getPoints(scratch.data(), static_cast<uint32_t>(scratch.size()));
            while (cnt == static_cast<uint32_t>(scratch.size())) {
                scratch.resize(scratch.size() * 2);
                cnt = lane->getPoints(scratch.data(), static_cast<uint32_t>(scratch.size()));
            }
            scratch.resize(cnt);
            existing = std::move(scratch);
        }

        // Remove points in [startSample, stopSample]
        for (const auto& pt : existing) {
            if (pt.positionSample >= startSample && pt.positionSample <= stopSample) {
                lane->removePoint(pt.positionSample);
            }
        }

        // Add new points
        for (const auto& pt : points) {
            lane->addPoint(pt.positionSample, pt.value, pt.curveShape, pt.tension);
        }

        // Sync to RT processor
        pushToProcessor(lane, targetNodeId, parameterIndex);
    }

private:
    std::string queryParameterName(NodeID nodeId, uint32_t parameterIndex) const {
        if (DSP::ChannelStripFactory::getRegistry().get(nodeId)) {
            switch (parameterIndex) {
                case 0: return "Volume"; case 1: return "Pan";
                case 2: return "Mute";   case 3: return "Solo";
                default: return "";
            }
        }
        if (DSP::PannerFactory::getRegistry().get(nodeId)) {
            switch (parameterIndex) {
                case 0: return "Pan";   case 1: return "Width";
                case 2: return "Mode";  default: return "";
            }
        }
        if (DSP::SendFactory::getRegistry().get(nodeId)) {
            if (parameterIndex == 0) return "SendGain";
            return "";
        }
        if (DSP::MonitorSwitchFactory::getRegistry().get(nodeId)) {
            if (parameterIndex == 0) return "MonitorState";
            return "";
        }
        if (auto* ip = DSP::InsertPluginFactory::getRegistry().get(nodeId)) {
            if (parameterIndex == ::BYPASS_PARAMETER_INDEX) return "Bypass";
            if (ip->pluginInstance) {
                auto* plugin = static_cast<Layer3::IPlugin*>(ip->pluginInstance);
                ::ParameterInfo info{};
                if (plugin->getParameterInfo(parameterIndex, info)) return info.name;
            }
            return "";
        }
        if (auto* inst = DSP::getInstrumentSlotState(nodeId)) {
            if (parameterIndex == ::BYPASS_PARAMETER_INDEX) return "Bypass";
            if (inst->pluginInstance) {
                auto* plugin = static_cast<Layer3::IPlugin*>(inst->pluginInstance);
                ::ParameterInfo info{};
                if (plugin->getParameterInfo(parameterIndex, info)) return info.name;
            }
            return "";
        }
        return "";
    }

    void pushToProcessor(composition::IAutomationLane* lane,
                         NodeID targetNodeId, uint32_t parameterIndex) {
        // Gather all points
        std::vector<::AutomationPoint> scratch(4096);
        uint32_t count = lane->getPoints(scratch.data(), static_cast<uint32_t>(scratch.size()));
        while (count == static_cast<uint32_t>(scratch.size())) {
            scratch.resize(scratch.size() * 2);
            count = lane->getPoints(scratch.data(), static_cast<uint32_t>(scratch.size()));
        }
        scratch.resize(count);

        processor_->updatePlaybackPoints(targetNodeId, lane->getTarget().subNodeId, parameterIndex, scratch.data(), count);
    }



    ISessionManager* sessionManager_ = nullptr;
    Layer2::IStringRegistry* stringRegistry_;
    Layer3::IAutomationProcessor* processor_;
    // Thread safety: trackManager_->getAutomationManager() is internally thread-safe.
    // The caller (TrackController/AutomationController) holds its own mutex.
};

std::unique_ptr<IAutomationRecordingGateway> IAutomationRecordingGateway::create(
    ISessionManager* sessionManager,
    Layer2::IStringRegistry* stringRegistry,
    Layer3::IAutomationProcessor* processor
) {
    return std::make_unique<AutomationRecordingGateway>(sessionManager, stringRegistry, processor);
}

} // namespace bridge
