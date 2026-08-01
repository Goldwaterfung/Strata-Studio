#include "plugin_handler.h"
#include "Middle Bridge/tracks/itrack_controller.h"
#include "Middle Bridge/browser/ibrowser_controller.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string_view>
#include <vector>

namespace agentic {

namespace {

[[nodiscard]] std::string toLowerString(std::string_view str) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace

ExecutionResult PluginHandler::handleCommand(const ParsedArgs& args,
                                             bridge::ITrackController* trackController,
                                             bridge::IBrowserController* browserController) {
    std::string_view sub = args.getSubcommand();

    if (sub == "scan") {
        if (browserController) {
            browserController->refreshScanner();
        }
        std::size_t scannedCount = 0;
        if (trackController) {
            scannedCount = trackController->getAvailablePlugins().size();
        }
        return ExecutionResult::Success("PLUGIN_SCAN_COMPLETED", {{"scanned_count", std::to_string(scannedCount)}});
    }

    if (sub == "list") {
        if (!trackController) {
            return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "DAW_NOT_RUNNING", "Track controller unavailable.");
        }

        auto plugins = trackController->getAvailablePlugins();

        // 1. Filter by category
        std::string_view categoryOpt = args.getOption("--category");
        if (!categoryOpt.empty()) {
            std::string catLower = toLowerString(categoryOpt);
            std::erase_if(plugins, [&](const PluginDescriptor& p) {
                if (catLower == "effect" || catLower == "fx") {
                    return p.isInstrument();
                }
                if (catLower == "instrument" || catLower == "inst" || catLower == "synth") {
                    return !p.isInstrument();
                }
                return false;
            });
        }

        // 2. Filter by substring search
        std::string_view filterOpt = args.getOption("--filter");
        if (!filterOpt.empty()) {
            std::string filterLower = toLowerString(filterOpt);
            std::erase_if(plugins, [&](const PluginDescriptor& p) {
                std::string nameLower = toLowerString(p.name);
                std::string vendorLower = toLowerString(p.manufacturer);
                return nameLower.find(filterLower) == std::string::npos &&
                       vendorLower.find(filterLower) == std::string::npos;
            });
        }

        // 3. Pagination (--head, --tail)
        auto headOpt = ParsedArgs::parseUint32(args.getOption("--head"));
        auto tailOpt = ParsedArgs::parseUint32(args.getOption("--tail"));

        if (headOpt && static_cast<std::size_t>(*headOpt) < plugins.size()) {
            plugins.resize(static_cast<std::size_t>(*headOpt));
        } else if (tailOpt && static_cast<std::size_t>(*tailOpt) < plugins.size()) {
            std::size_t count = static_cast<std::size_t>(*tailOpt);
            auto offset = static_cast<std::ptrdiff_t>(plugins.size() - count);
            plugins.erase(plugins.begin(), plugins.begin() + offset);
        }

        std::vector<std::map<std::string, std::string>> rows;
        rows.reserve(plugins.size());
        for (const auto& plug : plugins) {
            rows.push_back({
                {"ID", std::to_string(plug.pluginId)},
                {"NAME", std::string(plug.name)},
                {"VENDOR", plug.manufacturer[0] != '\0' ? std::string(plug.manufacturer) : "Generic"},
                {"CATEGORY", plug.isInstrument() ? "Instrument" : "Effect"},
                {"FORMAT", (plug.formatFlags & 0x01) ? "VST3" : ((plug.formatFlags & 0x02) ? "AU" : "CLAP")}
            });
        }

        return ExecutionResult::MultiSuccess("PLUGIN_LIST", rows);
    }

    if (sub == "add") {
        if (!trackController) {
            return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "DAW_NOT_RUNNING", "Track controller unavailable.");
        }

        std::string_view trackStr = args.getOption("--track", "1");
        auto trackIds = ParsedArgs::parseIntegerRange(trackStr);
        if (trackIds.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Missing or invalid --track argument.");
        }

        std::string_view nameOpt = args.getOption("--name");
        std::string_view idOpt = args.getOption("--id");

        uint32_t resolvedPluginId = UINT32_MAX;
        std::string pluginName;

        if (!idOpt.empty()) {
            auto parsedId = ParsedArgs::parseUint32(idOpt);
            if (!parsedId) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Invalid numerical plugin --id.");
            }
            resolvedPluginId = *parsedId;
            pluginName = "ID:" + std::to_string(resolvedPluginId);
        } else if (!nameOpt.empty()) {
            // Strict exact case-sensitive lookup per grilling decisions
            resolvedPluginId = trackController->findPluginIdByName(nameOpt);
            if (resolvedPluginId == UINT32_MAX) {
                return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "ENTITY_NOT_FOUND",
                                               "Plugin with exact case-sensitive name '" + std::string(nameOpt) + "' was not found.");
            }
            pluginName = std::string(nameOpt);
        } else {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Must specify --name or --id to add plugin.");
        }

        uint32_t slotIdx = 0;
        std::string_view slotOpt = args.getOption("--slot");
        if (!slotOpt.empty()) {
            auto parsedSlot = ParsedArgs::parseUint32(slotOpt);
            if (!parsedSlot || *parsedSlot >= 8) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Slot index must be between 0 and 7.");
            }
            slotIdx = *parsedSlot;
        }

        for (uint32_t trkId : trackIds) {
            trackController->insertPlugin(TrackID{trkId, 1}, slotIdx, resolvedPluginId);
        }

        return ExecutionResult::Success("PLUGIN_ADDED", {
            {"track", std::string(trackStr)},
            {"slot", std::to_string(slotIdx)},
            {"plugin_id", std::to_string(resolvedPluginId)},
            {"plugin_name", pluginName}
        });
    }

    if (sub == "set-param") {
        if (!trackController) {
            return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "DAW_NOT_RUNNING", "Track controller unavailable.");
        }

        std::string_view trackStr = args.getOption("--track", "1");
        auto trackIdOpt = ParsedArgs::parseUint32(trackStr);
        if (!trackIdOpt) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Invalid --track specification.");
        }

        std::string_view slotStr = args.getOption("--plugin", args.getOption("--slot", "0"));
        auto slotOpt = ParsedArgs::parseUint32(slotStr);
        if (!slotOpt || *slotOpt >= 8) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Plugin slot index must be between 0 and 7.");
        }

        std::string_view paramStr = args.getOption("--param", "0");
        auto paramOpt = ParsedArgs::parseUint32(paramStr);
        if (!paramOpt) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Invalid --param index.");
        }

        std::string_view valStr = args.getOption("--val");
        if (valStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Missing required --val parameter value.");
        }

        auto valFloatOpt = ParsedArgs::parseFloat(valStr);
        if (!valFloatOpt) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Invalid numeric --val parameter value.");
        }

        float val = *valFloatOpt;
        if (val < 0.0f || val > 1.0f) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "--val parameter value must be normalized between 0.0 and 1.0.");
        }

        trackController->setPluginParameter(TrackID{*trackIdOpt, 1}, *slotOpt, *paramOpt, val);

        return ExecutionResult::Success("PLUGIN_PARAM_UPDATED", {
            {"track", std::to_string(*trackIdOpt)},
            {"slot", std::to_string(*slotOpt)},
            {"param_id", std::to_string(*paramOpt)},
            {"val", std::to_string(val)}
        });
    }

    if (sub == "copy") {
        if (!trackController) {
            return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "DAW_NOT_RUNNING", "Track controller unavailable.");
        }

        std::string_view fromTrackStr = args.getOption("--from-track", "1");
        auto fromTrackOpt = ParsedArgs::parseUint32(fromTrackStr);
        if (!fromTrackOpt) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Invalid --from-track specification.");
        }

        std::string_view slotStr = args.getOption("--slot", "0");
        auto slotOpt = ParsedArgs::parseUint32(slotStr);
        if (!slotOpt || *slotOpt >= 8) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Slot index must be between 0 and 7.");
        }

        std::string_view toTracksStr = args.getOption("--to-tracks");
        auto toTracks = ParsedArgs::parseIntegerRange(toTracksStr);
        if (toTracks.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Missing or invalid --to-tracks specification.");
        }

        uint32_t srcSlotIdx = *slotOpt;
        TrackID srcTrackId{*fromTrackOpt, 1};

        uint32_t srcPluginId = trackController->getPluginIdInSlot(srcTrackId, srcSlotIdx);
        if (srcPluginId == UINT32_MAX) {
            return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "ENTITY_NOT_FOUND",
                                           "Source slot " + std::to_string(srcSlotIdx) + " on track " + std::to_string(*fromTrackOpt) + " is empty.");
        }

        bool overwrite = args.hasFlag("--overwrite");

        // Overwrite validation check per grilling session
        if (!overwrite) {
            for (uint32_t dstTrk : toTracks) {
                if (trackController->getPluginIdInSlot(TrackID{dstTrk, 1}, srcSlotIdx) != UINT32_MAX) {
                    return ExecutionResult::Error(ErrorCode::RESOURCE_BUSY_USER_TOUCH, "RESOURCE_BUSY",
                                                   "Destination slot " + std::to_string(srcSlotIdx) + " on track " + std::to_string(dstTrk) +
                                                   " is occupied. Pass --overwrite flag to replace.");
                }
            }
        }

        auto state = trackController->getPluginState(srcTrackId, srcSlotIdx);

        for (uint32_t dstTrk : toTracks) {
            TrackID dstTrackId{dstTrk, 1};
            trackController->insertPlugin(dstTrackId, srcSlotIdx, srcPluginId);
            if (!state.empty()) {
                trackController->setPluginState(dstTrackId, srcSlotIdx, state);
            }
        }

        return ExecutionResult::Success("PLUGIN_COPIED", {
            {"from_track", std::to_string(*fromTrackOpt)},
            {"slot", std::to_string(srcSlotIdx)},
            {"to_tracks", std::string(toTracksStr)}
        });
    }

    if (sub == "copy-chain") {
        if (!trackController) {
            return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "DAW_NOT_RUNNING", "Track controller unavailable.");
        }

        std::string_view fromTrackStr = args.getOption("--from-track", "1");
        auto fromTrackOpt = ParsedArgs::parseUint32(fromTrackStr);
        if (!fromTrackOpt) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Invalid --from-track specification.");
        }

        std::string_view toTracksStr = args.getOption("--to-tracks");
        auto toTracks = ParsedArgs::parseIntegerRange(toTracksStr);
        if (toTracks.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Missing or invalid --to-tracks specification.");
        }

        TrackID srcTrackId{*fromTrackOpt, 1};
        bool overwrite = args.hasFlag("--overwrite");

        // Validate destination slot availability across active slots
        if (!overwrite) {
            for (uint32_t s = 0; s < 8; ++s) {
                if (trackController->getPluginIdInSlot(srcTrackId, s) != UINT32_MAX) {
                    for (uint32_t dstTrk : toTracks) {
                        if (trackController->getPluginIdInSlot(TrackID{dstTrk, 1}, s) != UINT32_MAX) {
                            return ExecutionResult::Error(ErrorCode::RESOURCE_BUSY_USER_TOUCH, "RESOURCE_BUSY",
                                                           "Destination slot " + std::to_string(s) + " on track " + std::to_string(dstTrk) +
                                                           " is occupied. Pass --overwrite flag to replace.");
                        }
                    }
                }
            }
        }

        // Copy all active slots
        uint32_t copiedCount = 0;
        for (uint32_t s = 0; s < 8; ++s) {
            uint32_t srcPluginId = trackController->getPluginIdInSlot(srcTrackId, s);
            if (srcPluginId != UINT32_MAX) {
                auto state = trackController->getPluginState(srcTrackId, s);
                for (uint32_t dstTrk : toTracks) {
                    TrackID dstTrackId{dstTrk, 1};
                    trackController->insertPlugin(dstTrackId, s, srcPluginId);
                    if (!state.empty()) {
                        trackController->setPluginState(dstTrackId, s, state);
                    }
                }
                copiedCount++;
            }
        }

        return ExecutionResult::Success("PLUGIN_CHAIN_COPIED", {
            {"from_track", std::to_string(*fromTrackOpt)},
            {"copied_slots", std::to_string(copiedCount)},
            {"to_tracks", std::string(toTracksStr)}
        });
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Unknown plugin subcommand.");
}

} // namespace agentic
