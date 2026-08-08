#include "routing_handler.h"
#include "Middle Bridge/tracks/itrack_controller.h"
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace agentic {

namespace {

[[nodiscard]] std::optional<TrackID> resolveTrackID(bridge::ITrackController* trackController, uint32_t idInt) {
    if (trackController == nullptr) return std::nullopt;
    auto tracks = trackController->getAllTracks();
    for (const auto& t : tracks) {
        if (t.trackId.id == idInt) {
            return t.trackId;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string trackTypeToString(composition::TrackType type) noexcept {
    switch (type) {
        case composition::TrackType::AUDIO:      return "AUDIO";
        case composition::TrackType::MIDI:       return "MIDI";
        case composition::TrackType::INSTRUMENT: return "INSTRUMENT";
        case composition::TrackType::AUX:        return "AUX";
        case composition::TrackType::MASTER:     return "MASTER";
        case composition::TrackType::FOLDER:     return "FOLDER";
        default: return "AUDIO";
    }
}

} // namespace

ExecutionResult RoutingHandler::handleCommand(const ParsedArgs& args, bridge::ITrackController* trackController) {
    if (trackController == nullptr) {
        return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Bridge track controller is not initialized.");
    }

    std::string_view sub = args.getSubcommand();

    if (sub == "folder") {
        std::string_view tracksStr = args.getOption("--track");
        std::string_view toBusStr = args.getOption("--to");

        if (tracksStr.empty() || toBusStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Subcommand 'route folder' requires --track and --to.");
        }

        auto sourceTrackIds = ParsedArgs::parseIntegerRange(tracksStr);
        auto toIds = ParsedArgs::parseIntegerRange(toBusStr);

        if (sourceTrackIds.empty() || toIds.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Invalid track or destination bus range provided.");
        }

        uint32_t destFolderIdInt = toIds.front();
        auto destFolderIdOpt = resolveTrackID(trackController, destFolderIdInt);
        if (!destFolderIdOpt.has_value()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Destination bus track " + std::to_string(destFolderIdInt) + " not found.");
        }
        TrackID destFolderId = *destFolderIdOpt;

        for (uint32_t srcId : sourceTrackIds) {
            auto srcTrackIdOpt = resolveTrackID(trackController, srcId);
            if (!srcTrackIdOpt.has_value()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Source track " + std::to_string(srcId) + " not found.");
            }
            trackController->setTrackParentFolder(*srcTrackIdOpt, destFolderId);
            trackController->setTrackOutputRouting(*srcTrackIdOpt, destFolderId);
        }

        return ExecutionResult::Success("ROUTE_FOLDER_UPDATED", {
            {"source_tracks", std::string(tracksStr)},
            {"destination_bus", std::string(toBusStr)}
        });
    }

    if (sub == "send") {
        std::string_view fromTracksStr = args.getOption("--from");
        std::string_view toAuxStr = args.getOption("--to");
        std::string_view dbStr = args.getOption("--db");
        std::string_view tapStr = args.getOption("--tap");

        if (fromTracksStr.empty() || toAuxStr.empty() || dbStr.empty() || tapStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Subcommand 'route send' requires --from, --to, --db, and --tap.");
        }

        auto sourceTrackIds = ParsedArgs::parseIntegerRange(fromTracksStr);
        auto toIds = ParsedArgs::parseIntegerRange(toAuxStr);

        if (sourceTrackIds.empty() || toIds.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Invalid source or aux track range provided.");
        }

        auto dbOpt = ParsedArgs::parseFloat(dbStr);
        if (!dbOpt.has_value()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Invalid dB gain value: " + std::string(dbStr));
        }

        float db = *dbOpt;
        float linearGain = std::pow(10.0f, db / 20.0f);
        bool isPreFader = (tapStr == "pre");

        uint32_t destAuxIdInt = toIds.front();
        auto destAuxIdOpt = resolveTrackID(trackController, destAuxIdInt);
        if (!destAuxIdOpt.has_value()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Aux track " + std::to_string(destAuxIdInt) + " not found.");
        }

        auto auxState = trackController->getTrackState(*destAuxIdOpt);
        NodeID destNodeId = auxState.channelStripNode;
        if (!destNodeId.isValid()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Aux track " + std::to_string(destAuxIdInt) + " does not have a valid DSP node.");
        }

        for (uint32_t srcId : sourceTrackIds) {
            auto srcTrackIdOpt = resolveTrackID(trackController, srcId);
            if (!srcTrackIdOpt.has_value()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Source track " + std::to_string(srcId) + " not found.");
            }
            TrackID srcTrackId = *srcTrackIdOpt;
            auto srcState = trackController->getTrackState(srcTrackId);

            uint32_t slotIndex = 0;
            uint32_t count = isPreFader ? srcState.activePreFaderSendCount : srcState.activePostFaderSendCount;
            const auto* sends = isPreFader ? srcState.preFaderSends : srcState.postFaderSends;

            for (uint32_t i = 0; i < count; ++i) {
                if (sends[i].destinationNodeId == destNodeId) {
                    slotIndex = i;
                    break;
                }
                if (!sends[i].isEnabled || !sends[i].destinationNodeId.isValid()) {
                    slotIndex = i;
                    break;
                }
                if (i + 1 < bridge::MAX_SEND_SLOTS) {
                    slotIndex = i + 1;
                }
            }

            trackController->setSendDestination(srcTrackId, isPreFader, slotIndex, destNodeId);
            trackController->setSendGain(srcTrackId, isPreFader, slotIndex, linearGain);
            trackController->setSendEnabled(srcTrackId, isPreFader, slotIndex, true);
        }

        std::stringstream ssDb;
        ssDb << std::fixed << std::setprecision(1) << db;

        return ExecutionResult::Success("ROUTE_SEND_ADDED", {
            {"from_tracks", std::string(fromTracksStr)},
            {"to_aux", std::string(toAuxStr)},
            {"gain_db", ssDb.str()},
            {"tap_point", std::string(tapStr)}
        });
    }

    if (sub == "sidechain") {
        std::string_view sourceStr = args.getOption("--source");
        std::string_view toTrackStr = args.getOption("--to-track");
        std::string_view slotStr = args.getOption("--slot");
        std::string_view dbStr = args.getOption("--db");

        if (sourceStr.empty() || toTrackStr.empty() || slotStr.empty() || dbStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Subcommand 'route sidechain' requires --source, --to-track, --slot, and --db.");
        }

        auto sourceIds = ParsedArgs::parseIntegerRange(sourceStr);
        auto toTrackIds = ParsedArgs::parseIntegerRange(toTrackStr);

        if (sourceIds.empty() || toTrackIds.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Invalid source or target track range provided.");
        }

        auto slotOpt = ParsedArgs::parseUint32(slotStr);
        if (!slotOpt.has_value()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Invalid plugin slot index: " + std::string(slotStr));
        }

        auto dbOpt = ParsedArgs::parseFloat(dbStr);
        if (!dbOpt.has_value()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Invalid sidechain gain dB: " + std::string(dbStr));
        }

        uint32_t slotIndex = *slotOpt;
        float gainDb = *dbOpt;

        auto sourceTrackIdOpt = resolveTrackID(trackController, sourceIds.front());
        if (!sourceTrackIdOpt.has_value()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Trigger source track " + std::to_string(sourceIds.front()) + " not found.");
        }

        auto targetTrackIdOpt = resolveTrackID(trackController, toTrackIds.front());
        if (!targetTrackIdOpt.has_value()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Target track " + std::to_string(toTrackIds.front()) + " not found.");
        }

        trackController->setPluginSidechainSource(*targetTrackIdOpt, slotIndex, *sourceTrackIdOpt, gainDb);

        return ExecutionResult::Success("ROUTE_SIDECHAIN_LINKED", {
            {"trigger_source", std::string(sourceStr)},
            {"target_track", std::string(toTrackStr)},
            {"target_slot", std::to_string(slotIndex)}
        });
    }

    if (sub == "list") {
        std::vector<std::map<std::string, std::string>> rows;

        auto tracks = trackController->getAllTracks();
        for (const auto& t : tracks) {
            std::string mainOutput = "Master";
            if (t.outputTargetTrackId.isValid()) {
                mainOutput = "Track " + std::to_string(t.outputTargetTrackId.id);
            } else if (t.parentFolderId.isValid()) {
                mainOutput = "Folder (" + std::to_string(t.parentFolderId.id) + ")";
            }

            std::stringstream sendsSs;
            bool firstSend = true;
            auto appendSends = [&](const bridge::SendSlotUIState* sends, uint32_t count, const char* tapLabel) {
                for (uint32_t i = 0; i < count; ++i) {
                    if (sends[i].isEnabled && sends[i].destinationNodeId.isValid()) {
                        if (!firstSend) sendsSs << ", ";
                        firstSend = false;
                        sendsSs << (sends[i].destinationName[0] != '\0' ? sends[i].destinationName : ("Node " + std::to_string(sends[i].destinationNodeId.id)));
                        sendsSs << " (" << std::fixed << std::setprecision(1) << sends[i].leveldB << "dB, " << tapLabel << ")";
                    }
                }
            };

            appendSends(t.preFaderSends, t.activePreFaderSendCount, "PRE");
            appendSends(t.postFaderSends, t.activePostFaderSendCount, "POST");

            std::stringstream scSs;
            bool firstSc = true;
            if (t.instrument.sidechain.isConnected) {
                scSs << "Inst: " << (t.instrument.sidechain.sourceTrackName[0] != '\0' ? t.instrument.sidechain.sourceTrackName : ("Track " + std::to_string(t.instrument.sidechain.sourceTrackId.id)));
                firstSc = false;
            }
            for (uint32_t i = 0; i < t.activePluginCount; ++i) {
                if (t.plugins[i].sidechain.isConnected) {
                    if (!firstSc) scSs << ", ";
                    firstSc = false;
                    scSs << "Slot " << i << " <- " << (t.plugins[i].sidechain.sourceTrackName[0] != '\0' ? t.plugins[i].sidechain.sourceTrackName : ("Track " + std::to_string(t.plugins[i].sidechain.sourceTrackId.id)));
                }
            }

            rows.push_back({
                {"ID", std::to_string(t.trackId.id)},
                {"NAME", std::string(t.name)},
                {"TYPE", trackTypeToString(t.type)},
                {"MAIN_OUTPUT", mainOutput},
                {"SENDS", sendsSs.str().empty() ? "None" : sendsSs.str()},
                {"SIDECHAINS", scSs.str().empty() ? "None" : scSs.str()}
            });
        }

        return ExecutionResult::MultiSuccess("ROUTE_LIST", rows);
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Unknown route subcommand.");
}

} // namespace agentic
