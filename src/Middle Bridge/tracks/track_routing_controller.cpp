#include "tracks/track_routing_controller.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/audio_input/audio_input_node.h"
#include "Core audio engine/transport/itransport.h"
#include "common/math/gain.h"
#include <cstring>
#include <algorithm>

namespace bridge {

TrackRoutingController::TrackRoutingController(TrackControllerContext context) : ctx_(context) {}

void TrackRoutingController::setSendGain(TrackID trackId, bool isPreFader,
                                   uint32_t sendIndex, float gainLinear) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    trackManager->setTrackSendGain(trackId, isPreFader, sendIndex, gainLinear);

    if (sendIndex < MAX_TRACK_SENDS && ctx_.recordingGateway) {
        auto desc = trackManager->getPipelineDescriptor(trackId);
        if (desc.trackNode.isValid()) {
            uint64_t playhead = ctx_.transport ? ctx_.transport->getPosition() : 0;
            float normalized = Math::Gain::linearToNormalized(gainLinear);
            uint32_t paramIdx = static_cast<uint32_t>(TrackMacroParameter::Send0Gain) + (sendIndex * 3);
            ctx_.recordingGateway->recordValue(trackId, desc.trackNode, paramIdx, playhead, normalized, ::AutomationPoint::Shape::LINEAR);
        }
    }
}

void TrackRoutingController::setSendPan(TrackID trackId, bool isPreFader,
                                  uint32_t sendIndex, float panPosition) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    trackManager->setTrackSendPan(trackId, isPreFader, sendIndex, panPosition);

    if (sendIndex < MAX_TRACK_SENDS && ctx_.recordingGateway) {
        auto desc = trackManager->getPipelineDescriptor(trackId);
        if (desc.trackNode.isValid()) {
            uint64_t playhead = ctx_.transport ? ctx_.transport->getPosition() : 0;
            uint32_t paramIdx = static_cast<uint32_t>(TrackMacroParameter::Send0Pan) + (sendIndex * 3);
            ctx_.recordingGateway->recordValue(trackId, desc.trackNode, paramIdx, playhead, panPosition, ::AutomationPoint::Shape::LINEAR);
        }
    }
}

void TrackRoutingController::setSendEnabled(TrackID trackId, bool isPreFader,
                                      uint32_t sendIndex, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    trackManager->setTrackSendEnabled(trackId, isPreFader, sendIndex, enabled);

    if (sendIndex < MAX_TRACK_SENDS && ctx_.recordingGateway) {
        auto desc = trackManager->getPipelineDescriptor(trackId);
        if (desc.trackNode.isValid()) {
            uint64_t playhead = ctx_.transport ? ctx_.transport->getPosition() : 0;
            float gainLinear = trackManager->getTrackSendGain(trackId, isPreFader, sendIndex);
            float targetGain = enabled ? gainLinear : 0.0f;
            float normalized = Math::Gain::linearToNormalized(targetGain);
            uint32_t paramIdx = static_cast<uint32_t>(TrackMacroParameter::Send0Gain) + (sendIndex * 3);
            ctx_.recordingGateway->recordValue(trackId, desc.trackNode, paramIdx, playhead, normalized, ::AutomationPoint::Shape::STEP);
        }
    }
}

void TrackRoutingController::setSendDestination(TrackID trackId, bool isPreFader,
                                          uint32_t sendIndex,
                                          NodeID destinationNodeId) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    // Resolve if destinationNodeId is actually a TrackID (or Master sentinel)
    if (destinationNodeId.isValid()) {
        if ((destinationNodeId.id == 0 && destinationNodeId.generation == 0) || 
            destinationNodeId == ctx_.masterChannelStripNode) {
            destinationNodeId = ctx_.masterChannelStripNode;
        } else {
            TrackID targetTrackId{destinationNodeId.id, destinationNodeId.generation};
            composition::TrackCreateInfo info{};
            if (trackManager->getTrackInfo(targetTrackId, info)) {
                if (info.type != composition::TrackType::AUX && info.type != composition::TrackType::MASTER) {
                    destinationNodeId = NodeID::invalid();
                } else {
                    auto pdesc = trackManager->getPipelineDescriptor(targetTrackId);
                    destinationNodeId = pdesc.trackNode;
                }
            } else {
                destinationNodeId = NodeID::invalid();
            }
        }
    }

    trackManager->setTrackSendDestination(trackId, isPreFader, sendIndex, destinationNodeId);
}



void TrackRoutingController::setTrackAudioInput(TrackID trackId, uint32_t mappedPhysicalInputIndex, uint32_t numChannels) {
    setTrackInput(trackId, mappedPhysicalInputIndex, numChannels);
}

void TrackRoutingController::setTrackInput(TrackID trackId, uint32_t optionId, uint32_t numChannels) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (trackManager) {
        trackManager->setTrackAudioInputChannel(trackId, optionId, numChannels);
    }
}

void TrackRoutingController::setPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex, TrackID sourceTrackId, float sendGaindB) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    float gainLinear = Math::Gain::dBToCoeff(sendGaindB);
    trackManager->setTrackSidechainRouting(targetTrackId, slotIndex, sourceTrackId, gainLinear);
}

void TrackRoutingController::clearPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex) {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    trackManager->clearTrackSidechainRouting(targetTrackId, slotIndex);
}

std::vector<TrackInputOption> TrackRoutingController::getAvailableSidechainSources(TrackID targetTrackId) const {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    std::vector<TrackInputOption> options;

    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return options;

    auto allTracks = trackManager->getAllTrackIDs();
    for (const auto& trackId : allTracks) {
        if (trackId == targetTrackId) continue;

        if (trackManager->detectFeedbackCycle(trackId, targetTrackId)) {
            continue;
        }

        composition::TrackCreateInfo info{};
        if (trackManager->getTrackInfo(trackId, info)) {
            TrackInputOption option{};
            option.optionId = trackId.id;
            option.numChannels = info.audioChannelCount > 0 ? info.audioChannelCount : 2;

            if (ctx_.stringRegistry && info.nameId != 0) {
                std::string nameStr;
                if (ctx_.stringRegistry->getString(info.nameId, nameStr)) {
                    option.name = nameStr;
                } else {
                    option.name = "Track " + std::to_string(trackId.id);
                }
            } else {
                option.name = "Track " + std::to_string(trackId.id);
            }
            options.push_back(std::move(option));
        }
    }
    return options;
}

SidechainSlotUIState TrackRoutingController::getPluginSidechainState(TrackID targetTrackId, uint32_t slotIndex) const {
    std::lock_guard<std::recursive_mutex> lock(ctx_.mutex);
    SidechainSlotUIState state{};
    state.hasSidechainInput = true;

    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return state;

    TrackID sourceTrackId{};
    float gainLinear = 1.0f;
    if (trackManager->getTrackSidechainRouting(targetTrackId, slotIndex, sourceTrackId, gainLinear)) {
        state.isConnected = true;
        state.sourceTrackId = sourceTrackId;
        state.sendGaindB = Math::Gain::coeffTodB(gainLinear);

        composition::TrackCreateInfo info{};
        if (trackManager->getTrackInfo(sourceTrackId, info) && ctx_.stringRegistry && info.nameId != 0) {
            std::string nameStr;
            if (ctx_.stringRegistry->getString(info.nameId, nameStr)) {
                std::strncpy(state.sourceTrackName, nameStr.c_str(), sizeof(state.sourceTrackName) - 1);
                state.sourceTrackName[sizeof(state.sourceTrackName) - 1] = '\0';
            }
        }
    }

    return state;
}

} // namespace bridge

