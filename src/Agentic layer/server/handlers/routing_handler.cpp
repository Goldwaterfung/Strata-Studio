#include "routing_handler.h"

namespace agentic {

ExecutionResult RoutingHandler::handleCommand(const ParsedArgs& args) {
    std::string_view sub = args.getSubcommand();

    if (sub == "folder") {
        std::string_view tracks = args.getOption("--track", "1..4");
        std::string_view toBus = args.getOption("--to", "5");
        return ExecutionResult::Success("ROUTE_FOLDER_UPDATED", {
            {"source_tracks", std::string(tracks)},
            {"destination_bus", std::string(toBus)}
        });
    }

    if (sub == "send") {
        std::string_view fromTracks = args.getOption("--from", "1..4");
        std::string_view toAux = args.getOption("--to", "10");
        std::string_view db = args.getOption("--db", "-12.0");
        std::string_view tap = args.getOption("--tap", "post");
        return ExecutionResult::Success("ROUTE_SEND_ADDED", {
            {"from_tracks", std::string(fromTracks)},
            {"to_aux", std::string(toAux)},
            {"gain_db", std::string(db)},
            {"tap_point", std::string(tap)}
        });
    }

    if (sub == "sidechain") {
        std::string_view source = args.getOption("--source", "1");
        std::string_view toTrack = args.getOption("--to-track", "2");
        std::string_view slot = args.getOption("--slot", "0");
        return ExecutionResult::Success("ROUTE_SIDECHAIN_LINKED", {
            {"trigger_source", std::string(source)},
            {"target_track", std::string(toTrack)},
            {"target_slot", std::string(slot)}
        });
    }

    if (sub == "list") {
        return ExecutionResult::Success("ROUTE_LIST", {
            {"ID", "1"},
            {"NAME", "Kick"},
            {"TYPE", "AUDIO"},
            {"MAIN_OUTPUT", "Bus (5)"},
            {"SENDS", "Aux 10 (-12dB, POST)"},
            {"SIDECHAINS", "-> Track 2 (Slot 0)"}
        });
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Unknown route subcommand.");
}

} // namespace agentic
