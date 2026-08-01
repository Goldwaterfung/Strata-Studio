#include "tracks/track_mixer_controller.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "recording/irecording_controller.h"
#include "Media management/registry/imedia_registry.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
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

TrackMixerController::TrackMixerController(TrackControllerContext context) : ctx_(context) {}



void TrackMixerController::setFaderGain(TrackID trackId, float gainLinear) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->setTrackFaderGain(trackId, gainLinear);

        NodeID targetNode = NodeID::invalid();
        if (trackId.id == 0 && trackId.generation == 0) {
            targetNode = ctx_.masterChannelStripNode;
        } else {
            auto desc = trackManager->getPipelineDescriptor(trackId);
            targetNode = desc.trackNode;
        }

        if (targetNode.isValid() && ctx_.recordingGateway) {
            uint64_t playhead = ctx_.transport ? ctx_.transport->getPosition() : 0;
            float normalized = Math::Gain::linearToNormalized(gainLinear);
            ctx_.recordingGateway->recordValue(trackId, targetNode, static_cast<uint32_t>(TrackMacroParameter::Volume), playhead, normalized, ::AutomationPoint::Shape::LINEAR);
        }
    }
}

void TrackMixerController::setPan(TrackID trackId, float panPosition) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->setTrackPan(trackId, panPosition);

        NodeID targetNode = NodeID::invalid();
        if (trackId.id == 0 && trackId.generation == 0) {
            targetNode = ctx_.masterChannelStripNode;
        } else {
            auto desc = trackManager->getPipelineDescriptor(trackId);
            targetNode = desc.trackNode;
        }

        if (targetNode.isValid() && ctx_.recordingGateway) {
            uint64_t playhead = ctx_.transport ? ctx_.transport->getPosition() : 0;
            ctx_.recordingGateway->recordValue(trackId, targetNode, static_cast<uint32_t>(TrackMacroParameter::Pan), playhead, panPosition, ::AutomationPoint::Shape::LINEAR);
        }
    }
}

void TrackMixerController::setMute(TrackID trackId, bool mute) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->setTrackMute(trackId, mute);

        NodeID targetNode = NodeID::invalid();
        if (trackId.id == 0 && trackId.generation == 0) {
            targetNode = ctx_.masterChannelStripNode;
        } else {
            auto desc = trackManager->getPipelineDescriptor(trackId);
            targetNode = desc.trackNode;
        }

        if (targetNode.isValid() && ctx_.recordingGateway) {
            uint64_t playhead = ctx_.transport ? ctx_.transport->getPosition() : 0;
            float val = mute ? 1.0f : 0.0f;
            ctx_.recordingGateway->recordValue(trackId, targetNode, static_cast<uint32_t>(TrackMacroParameter::Mute), playhead, val, ::AutomationPoint::Shape::STEP);
        }
    }
}

void TrackMixerController::setSolo(TrackID trackId, bool solo) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->setTrackSolo(trackId, solo);
    }
}

void TrackMixerController::setRecordArmed(TrackID trackId, bool armed) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    trackManager->setTrackRecordArmed(trackId, armed);

    auto queue = ctx_.recordingController ? ctx_.recordingController->prepareTrackForRecording(trackId, armed) : nullptr;

    composition::TrackCreateInfo info{};
    if (trackManager->getTrackInfo(trackId, info)) {
        auto desc = trackManager->getPipelineDescriptor(trackId);
        if (desc.trackNode.isValid()) {
            uint8_t mState = info.isInputMonitoring ? 1 : 0; // Strictly ON or OFF

            SystemMutation mut{};
            mut.type = Layer2::MutationType::MONITOR_STATE_SET;
            mut.targetId = desc.trackNode;
            mut.priority = 0;
            mut.monitor.monitorState = mState;
            if (ctx_.mutationBridge) ctx_.mutationBridge->pushMutation(mut);
        }
        if (desc.instrumentSlotNode.isValid()) {
            bool acceptsMidi = info.isInputMonitoring || armed;
            SystemMutation mut{};
            mut.type = Layer2::MutationType::MONITOR_STATE_SET;
            mut.targetId = desc.instrumentSlotNode;
            mut.priority = 0;
            mut.monitor.monitorState = acceptsMidi ? 1 : 0;
            if (ctx_.mutationBridge) ctx_.mutationBridge->pushMutation(mut);
        }
        if (desc.audioInputNode.isValid()) {
            SystemMutation mut{};
            mut.type = Layer2::MutationType::RECORD_ARM_SET;
            mut.targetId = desc.audioInputNode;
            mut.priority = 0;
            mut.record.isArmed = armed;
            mut.record.recordingQueue = queue.get();
            if (ctx_.mutationBridge) ctx_.mutationBridge->pushMutation(mut);
        }
    }
}

void TrackMixerController::setInputMonitoring(TrackID trackId, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    trackManager->setTrackInputMonitoring(trackId, enabled);

    composition::TrackCreateInfo info{};
    if (trackManager->getTrackInfo(trackId, info)) {
        auto desc = trackManager->getPipelineDescriptor(trackId);
        if (desc.trackNode.isValid()) {
            uint8_t mState = info.isInputMonitoring ? 1 : 0; // Strictly ON or OFF

            SystemMutation mut{};
            mut.type = Layer2::MutationType::MONITOR_STATE_SET;
            mut.targetId = desc.trackNode;
            mut.priority = 0;
            mut.monitor.monitorState = mState;
            if (ctx_.mutationBridge) ctx_.mutationBridge->pushMutation(mut);
        }
        if (desc.instrumentSlotNode.isValid()) {
            bool acceptsMidi = enabled || info.isRecordArmed;
            SystemMutation mut{};
            mut.type = Layer2::MutationType::MONITOR_STATE_SET;
            mut.targetId = desc.instrumentSlotNode;
            mut.priority = 0;
            mut.monitor.monitorState = acceptsMidi ? 1 : 0;
            if (ctx_.mutationBridge) ctx_.mutationBridge->pushMutation(mut);
        }
    }
}

} // namespace bridge
