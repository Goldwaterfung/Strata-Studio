#include "track_handler.h"
#include "Middle Bridge/tracks/itrack_controller.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace agentic {

namespace {

[[nodiscard]] uint32_t parseColorARGB(std::string_view colorStr) noexcept {
    if (colorStr.empty()) return 0xFF4A90E2; // Default blue

    std::string str(colorStr);
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    static const std::unordered_map<std::string_view, uint32_t> colorTable = {
        {"red",    0xFFE74C3C},
        {"blue",   0xFF3498DB},
        {"green",  0xFF2ECC71},
        {"yellow", 0xFFF1C40F},
        {"purple", 0xFF9B59B6},
        {"orange", 0xFFE67E22},
        {"cyan",   0xFF1ABC9C},
        {"pink",   0xFFFD79A8},
        {"white",  0xFFECF0F1},
        {"grey",   0xFF95A5A6},
        {"gray",   0xFF95A5A6},
        {"black",  0xFF2C3E50}
    };

    auto it = colorTable.find(str);
    if (it != colorTable.end()) {
        return it->second;
    }

    if (str.starts_with('#') && str.size() == 7) {
        try {
            uint32_t rgb = static_cast<uint32_t>(std::stoul(str.substr(1), nullptr, 16));
            return 0xFF000000 | rgb;
        } catch (...) {}
    }

    return 0xFF4A90E2;
}

[[nodiscard]] uint32_t autoColorForTrackName(std::string_view name) noexcept {
    std::string str(name);
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (str.find("kick") != std::string::npos || str.find("snare") != std::string::npos ||
        str.find("drum") != std::string::npos || str.find("hh") != std::string::npos ||
        str.find("hihat") != std::string::npos || str.find("tom") != std::string::npos ||
        str.find("perc") != std::string::npos) {
        return 0xFFE74C3C; // Red for drums
    }
    if (str.find("bass") != std::string::npos || str.find("sub") != std::string::npos) {
        return 0xFF9B59B6; // Purple for bass
    }
    if (str.find("vox") != std::string::npos || str.find("vocal") != std::string::npos ||
        str.find("lead") != std::string::npos) {
        return 0xFFF1C40F; // Yellow for vocals
    }
    if (str.find("gtr") != std::string::npos || str.find("guitar") != std::string::npos ||
        str.find("acg") != std::string::npos) {
        return 0xFF2ECC71; // Green for guitars
    }
    if (str.find("syn") != std::string::npos || str.find("synth") != std::string::npos ||
        str.find("keys") != std::string::npos || str.find("piano") != std::string::npos) {
        return 0xFF1ABC9C; // Cyan for synths/keys
    }
    if (str.find("bus") != std::string::npos || str.find("aux") != std::string::npos ||
        str.find("fx") != std::string::npos || str.find("reverb") != std::string::npos) {
        return 0xFFE67E22; // Orange for aux/buses
    }

    return 0xFF3498DB;
}

[[nodiscard]] std::string sanitizeTrackName(std::string_view name) {
    std::string str(name);
    constexpr std::array<std::string_view, 7> exts = {".wav", ".WAV", ".mp3", ".MP3", ".aif", ".aiff", ".flac"};
    for (const auto& ext : exts) {
        if (str.ends_with(ext)) {
            str.erase(str.size() - ext.size());
            break;
        }
    }
    std::replace(str.begin(), str.end(), '_', ' ');
    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.front()))) {
        str.erase(str.begin());
    }
    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.back()))) {
        str.pop_back();
    }
    if (str.empty()) return "Untitled Track";
    return str;
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

TrackID createTrackByType(bridge::ITrackController* controller, std::string_view type, const char* name, uint32_t color) {
    if (controller == nullptr) return TrackID{};
    if (type == "instrument" || type == "midi") {
        return controller->addInstrumentTrack(name, color);
    }
    if (type == "aux" || type == "bus") {
        return controller->addAuxTrack(name, color);
    }
    if (type == "folder") {
        return controller->addFolderTrack(name, color);
    }
    return controller->addAudioTrack(name, 2, color);
}

std::vector<uint32_t> requireTrackRange(const ParsedArgs& args, std::string_view optName, std::optional<ExecutionResult>& errOut) {
    std::string_view opt = args.getOption(optName);
    if (opt.empty()) {
        errOut = ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Missing required " + std::string(optName) + " argument.");
        return {};
    }
    auto trackIds = ParsedArgs::parseIntegerRange(opt);
    if (trackIds.empty()) {
        errOut = ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Invalid range format for " + std::string(optName) + ".");
        return {};
    }
    return trackIds;
}

} // namespace

ExecutionResult TrackHandler::handleCommand(const ParsedArgs& args, bridge::ITrackController* trackController) {
    std::string_view verb = args.getVerb();
    std::string_view sub = args.getSubcommand();

    if (sub == "create") {
        std::string_view name = args.getOption("--name");
        std::string_view type = args.getOption("--type");
        std::string_view colorStr = args.getOption("--color");

        if (name.empty() || type.empty() || colorStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Subcommand 'track create' requires --name, --type, and --color.");
        }

        uint32_t color = parseColorARGB(colorStr);
        TrackID newId = createTrackByType(trackController, type, std::string(name).c_str(), color);

        return ExecutionResult::Success("TRACK_CREATED", {
            {"id", std::to_string(newId.id)},
            {"name", std::string(name)},
            {"type", std::string(type)}
        });
    }

    if (sub == "create-batch") {
        std::string_view namesStr = args.getOption("--names");
        std::string_view type = args.getOption("--type");
        if (namesStr.empty() || type.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Subcommand 'track create-batch' requires --names and --type.");
        }

        std::vector<std::map<std::string, std::string>> rows;
        std::string namesCopy{namesStr};
        std::stringstream ss{namesCopy};
        std::string nameItem;
        while (std::getline(ss, nameItem, ',')) {
            while (!nameItem.empty() && std::isspace(static_cast<unsigned char>(nameItem.front()))) {
                nameItem.erase(nameItem.begin());
            }
            while (!nameItem.empty() && std::isspace(static_cast<unsigned char>(nameItem.back()))) {
                nameItem.pop_back();
            }
            if (nameItem.empty()) continue;

            uint32_t color = autoColorForTrackName(nameItem);
            TrackID newId = createTrackByType(trackController, type, nameItem.c_str(), color);

            rows.push_back({
                {"ID", std::to_string(newId.id)},
                {"NAME", nameItem},
                {"TYPE", std::string(type)}
            });
        }

        return ExecutionResult::MultiSuccess("BATCH_TRACKS_CREATED", rows);
    }

    if (sub == "list") {
        std::vector<std::map<std::string, std::string>> rows;

        if (trackController != nullptr) {
            auto tracks = trackController->getAllTracks();
            for (const auto& t : tracks) {
                std::stringstream ssGain, ssPan;
                ssGain << std::fixed << std::setprecision(1) << t.faderLeveldB;
                ssPan << std::fixed << std::setprecision(2) << t.panPosition;

                rows.push_back({
                    {"ID", std::to_string(t.trackId.id)},
                    {"NAME", std::string(t.name)},
                    {"TYPE", trackTypeToString(t.type)},
                    {"GAIN_DB", ssGain.str()},
                    {"PAN", ssPan.str()},
                    {"MUTE", t.isMuted ? "true" : "false"},
                    {"SOLO", t.isSoloed ? "true" : "false"}
                });
            }
        }

        return ExecutionResult::MultiSuccess("TRACK_LIST", rows);
    }

    if (sub == "inspect") {
        std::optional<ExecutionResult> err;
        auto trackIds = requireTrackRange(args, "--track", err);
        if (trackIds.empty()) return *err;
        uint32_t trackIdInt = trackIds.front();

        std::map<std::string, std::string> fields;
        if (trackController != nullptr) {
            auto t = trackController->getTrackState(TrackID{trackIdInt, 1});
            std::stringstream ssGain, ssPan;
            ssGain << std::fixed << std::setprecision(1) << t.faderLeveldB;
            ssPan << std::fixed << std::setprecision(2) << t.panPosition;

            fields = {
                {"ID", std::to_string(t.trackId.id)},
                {"NAME", std::string(t.name)},
                {"TYPE", trackTypeToString(t.type)},
                {"GAIN_DB", ssGain.str()},
                {"PAN", ssPan.str()},
                {"MUTE", t.isMuted ? "true" : "false"},
                {"SOLO", t.isSoloed ? "true" : "false"},
                {"COLOR_ARGB", std::to_string(t.colorARGB)},
                {"RECORD_ARMED", t.isRecordArmed ? "true" : "false"},
                {"INPUT_MONITORING", t.isInputMonitoring ? "true" : "false"}
            };
        }

        return ExecutionResult::Success("TRACK_INSPECT", fields);
    }

    if (sub == "set-gain") {
        std::string_view trackStr = args.getOption("--track");
        std::string_view dbStr = args.getOption("--db");
        if (trackStr.empty() || dbStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Subcommand 'track set-gain' requires --track and --db.");
        }
        auto dbOpt = ParsedArgs::parseFloat(dbStr);
        if (!dbOpt.has_value()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Invalid numeric format for --db.");
        }
        float db = *dbOpt;
        std::optional<ExecutionResult> err;
        auto trackIds = requireTrackRange(args, "--track", err);
        if (trackIds.empty()) return *err;

        if (trackController != nullptr) {
            float linearGain = std::pow(10.0f, db / 20.0f);
            for (uint32_t tid : trackIds) {
                trackController->setFaderGain(TrackID{tid, 1}, linearGain);
            }
        }

        std::stringstream ssDb;
        ssDb << std::fixed << std::setprecision(1) << db;
        return ExecutionResult::Success("TRACK_GAIN_UPDATED", {
            {"tracks", std::string(trackStr)},
            {"gain_db", ssDb.str()}
        });
    }

    if (sub == "set-pan") {
        std::string_view trackStr = args.getOption("--track");
        std::string_view valStr = args.getOption("--value");
        if (trackStr.empty() || valStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Subcommand 'track set-pan' requires --track and --value.");
        }
        auto panOpt = ParsedArgs::parseFloat(valStr);
        if (!panOpt.has_value()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Invalid numeric format for --value.");
        }
        float panVal = *panOpt;
        std::optional<ExecutionResult> err;
        auto trackIds = requireTrackRange(args, "--track", err);
        if (trackIds.empty()) return *err;

        if (trackController != nullptr) {
            float normPan = (panVal + 1.0f) * 0.5f;
            for (uint32_t tid : trackIds) {
                trackController->setPan(TrackID{tid, 1}, normPan);
            }
        }

        return ExecutionResult::Success("TRACK_PAN_UPDATED", {
            {"tracks", std::string(trackStr)},
            {"pan", std::string(valStr)}
        });
    }

    if (sub == "set-mute") {
        std::string_view trackStr = args.getOption("--track");
        if (trackStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Missing required --track argument.");
        }
        bool on = args.hasFlag("on");
        std::optional<ExecutionResult> err;
        auto trackIds = requireTrackRange(args, "--track", err);
        if (trackIds.empty()) return *err;

        if (trackController != nullptr) {
            for (uint32_t tid : trackIds) {
                trackController->setMute(TrackID{tid, 1}, on);
            }
        }

        return ExecutionResult::Success("TRACK_MUTE_UPDATED", {
            {"tracks", std::string(trackStr)},
            {"muted", on ? "true" : "false"}
        });
    }

    if (sub == "set-solo") {
        std::string_view trackStr = args.getOption("--track");
        if (trackStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Missing required --track argument.");
        }
        bool on = args.hasFlag("on");
        std::optional<ExecutionResult> err;
        auto trackIds = requireTrackRange(args, "--track", err);
        if (trackIds.empty()) return *err;

        if (trackController != nullptr) {
            for (uint32_t tid : trackIds) {
                trackController->setSolo(TrackID{tid, 1}, on);
            }
        }

        return ExecutionResult::Success("TRACK_SOLO_UPDATED", {
            {"tracks", std::string(trackStr)},
            {"soloed", on ? "true" : "false"}
        });
    }

    if (sub == "delete") {
        std::string_view trackStr = args.getOption("--track");
        if (trackStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Missing required --track argument.");
        }
        std::optional<ExecutionResult> err;
        auto trackIds = requireTrackRange(args, "--track", err);
        if (trackIds.empty()) return *err;

        if (trackController != nullptr) {
            for (uint32_t tid : trackIds) {
                trackController->removeTrack(TrackID{tid, 1});
            }
        }

        return ExecutionResult::Success("TRACK_DELETED", {
            {"tracks", std::string(trackStr)}
        });
    }

    if (sub == "set-color") {
        std::string_view trackStr = args.getOption("--track");
        std::string_view colorStr = args.getOption("--color");
        if (trackStr.empty() || colorStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Subcommand 'track set-color' requires --track and --color.");
        }
        uint32_t color = parseColorARGB(colorStr);
        std::optional<ExecutionResult> err;
        auto trackIds = requireTrackRange(args, "--track", err);
        if (trackIds.empty()) return *err;

        if (trackController != nullptr) {
            for (uint32_t tid : trackIds) {
                trackController->setTrackColor(TrackID{tid, 1}, color);
            }
        }

        return ExecutionResult::Success("TRACK_COLOR_UPDATED", {
            {"tracks", std::string(trackStr)},
            {"color", std::string(colorStr)}
        });
    }

    if (sub == "auto-color") {
        std::size_t count = 0;
        if (trackController != nullptr) {
            auto tracks = trackController->getAllTracks();
            count = tracks.size();
            for (const auto& t : tracks) {
                uint32_t color = autoColorForTrackName(t.name);
                trackController->setTrackColor(t.trackId, color);
            }
        }
        return ExecutionResult::Success("TRACKS_AUTO_COLORED", {{"count", std::to_string(count)}});
    }

    if (sub == "sanitize-names") {
        std::size_t count = 0;
        if (trackController != nullptr) {
            auto tracks = trackController->getAllTracks();
            count = tracks.size();
            for (const auto& t : tracks) {
                std::string clean = sanitizeTrackName(t.name);
                trackController->renameTrack(t.trackId, clean.c_str());
            }
        }
        return ExecutionResult::Success("TRACK_NAMES_SANITIZED", {{"count", std::to_string(count)}});
    }

    if (verb == "prep" || sub == "gain-stage") {
        std::string_view trackStr = args.getOption("--track");
        std::string_view targetRmsStr = args.getOption("--target-rms");
        if (trackStr.empty() || targetRmsStr.empty()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Subcommand 'prep gain-stage' requires --track and --target-rms.");
        }
        auto targetRmsOpt = ParsedArgs::parseFloat(targetRmsStr);
        if (!targetRmsOpt.has_value()) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Invalid numeric format for --target-rms.");
        }
        float targetRms = *targetRmsOpt;
        std::optional<ExecutionResult> err;
        auto trackIds = requireTrackRange(args, "--track", err);
        if (trackIds.empty()) return *err;

        if (trackController != nullptr) {
            float linearGain = std::pow(10.0f, targetRms / 20.0f);
            for (uint32_t tid : trackIds) {
                trackController->setFaderGain(TrackID{tid, 1}, linearGain);
            }
        }

        std::stringstream ssRms;
        ssRms << std::fixed << std::setprecision(1) << targetRms;
        return ExecutionResult::Success("GAIN_STAGE_COMPLETED", {
            {"tracks", std::string(trackStr)},
            {"target_rms_db", ssRms.str()}
        });
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Unknown track subcommand.");
}

} // namespace agentic
