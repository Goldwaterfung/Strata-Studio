#include "clip_handler.h"

namespace agentic {

ExecutionResult ClipHandler::handleCommand(const ParsedArgs& args) {
    std::string_view sub = args.getSubcommand();

    if (sub == "add-audio") {
        std::string_view trackStr = args.getOption("--track", "1");
        std::string_view path = args.getOption("--path", "/audio/sample.wav");
        std::string_view start = args.getOption("--start", "0.0");
        return ExecutionResult::Success("CLIP_ADDED", {
            {"track", std::string(trackStr)},
            {"clip_id", "0"},
            {"path", std::string(path)},
            {"start", std::string(start)}
        });
    }

    if (sub == "split") {
        std::string_view trackStr = args.getOption("--track", "1");
        std::string_view clipStr = args.getOption("--clip", "0");
        std::string_view atStr = args.getOption("--at", "4.0");
        return ExecutionResult::Success("CLIP_SPLIT", {
            {"track", std::string(trackStr)},
            {"clip", std::string(clipStr)},
            {"split_at", std::string(atStr)}
        });
    }

    if (sub == "trim-silence") {
        std::string_view trackStr = args.getOption("--track", "1");
        std::string_view thresh = args.getOption("--threshold", "-48.0");
        return ExecutionResult::Success("CLIP_SILENCE_TRIMMED", {
            {"track", std::string(trackStr)},
            {"threshold_db", std::string(thresh)}
        });
    }

    if (sub == "quantize") {
        std::string_view trackStr = args.getOption("--track", "1");
        std::string_view grid = args.getOption("--grid", "1/16");
        return ExecutionResult::Success("CLIPS_QUANTIZED", {
            {"track", std::string(trackStr)},
            {"grid", std::string(grid)}
        });
    }

    if (sub == "merge") {
        std::string_view trackStr = args.getOption("--track", "1");
        std::string_view clips = args.getOption("--clips", "0..3");
        return ExecutionResult::Success("CLIPS_MERGED", {
            {"track", std::string(trackStr)},
            {"clips", std::string(clips)}
        });
    }

    if (sub == "move") {
        std::string_view trackStr = args.getOption("--track", "1");
        std::string_view clipStr = args.getOption("--clip", "0");
        std::string_view toPos = args.getOption("--to-pos", "8.1.1");
        return ExecutionResult::Success("CLIP_MOVED", {
            {"track", std::string(trackStr)},
            {"clip", std::string(clipStr)},
            {"to_pos", std::string(toPos)}
        });
    }

    if (sub == "nudge") {
        std::string_view trackStr = args.getOption("--track", "1");
        std::string_view byStr = args.getOption("--by", "+1/16");
        return ExecutionResult::Success("CLIP_NUDGED", {
            {"track", std::string(trackStr)},
            {"by", std::string(byStr)}
        });
    }

    if (sub == "set-gain") {
        std::string_view trackStr = args.getOption("--track", "1");
        std::string_view clipStr = args.getOption("--clip", "0");
        std::string_view db = args.getOption("--db", "0.0");
        return ExecutionResult::Success("CLIP_GAIN_UPDATED", {
            {"track", std::string(trackStr)},
            {"clip", std::string(clipStr)},
            {"gain_db", std::string(db)}
        });
    }

    if (sub == "list") {
        return ExecutionResult::Success("CLIP_LIST", {
            {"CLIP_ID", "0"},
            {"NAME", "Drums_Wav"},
            {"START_BAR", "1.1.1"},
            {"DURATION", "8.0.0"},
            {"GAIN_DB", "0.0"},
            {"MUTED", "false"}
        });
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Unknown clip subcommand.");
}

} // namespace agentic
