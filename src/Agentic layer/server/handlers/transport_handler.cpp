#include "transport_handler.h"
#include "Middle Bridge/timeline/itimeline_controller.h"
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

namespace agentic {

ExecutionResult TransportHandler::handleCommand(const ParsedArgs& args, bridge::ITimelineController* timeline) {
    std::string_view verb = args.getVerb();
    std::string_view sub = args.getSubcommand();

    if (verb == "status") {
        std::string state = "STOPPED";
        double bpm = 120.0;
        std::string timeSig = "4/4";
        std::string bbtStr = "1.1.1";
        std::string secStr = "0.000";

        if (timeline != nullptr) {
            state = timeline->isPlaying() ? "PLAYING" : "STOPPED";
            bpm = timeline->getBPM();

            uint8_t num = 4, den = 4;
            timeline->getTimeSignatureAtFrame(timeline->getCurrentFrame(), num, den);
            timeSig = std::to_string(num) + "/" + std::to_string(den);

            uint32_t bar = 1, beat = 1, tick = 1;
            timeline->getCurrentBBT(bar, beat, tick);
            bbtStr = std::to_string(bar) + "." + std::to_string(beat) + "." + std::to_string(tick);

            std::stringstream ssSec;
            ssSec << std::fixed << std::setprecision(3) << timeline->getCurrentSeconds();
            secStr = ssSec.str();
        }

        std::stringstream ssBpm;
        ssBpm << std::fixed << std::setprecision(2) << bpm;

        std::map<std::string, std::string> fields{
            {"TRANSPORT_STATE", state},
            {"TEMPO", ssBpm.str()},
            {"TIME_SIGNATURE", timeSig},
            {"POSITION_BARS", bbtStr},
            {"POSITION_SECONDS", secStr}
        };
        return ExecutionResult::Success("STATUS_OK", fields);
    }

    if (verb == "transport") {
        if (sub == "play") {
            if (timeline != nullptr) {
                timeline->play();
            }
            return ExecutionResult::Success("PLAYBACK_STARTED", {{"state", "playing"}});
        }
        if (sub == "stop") {
            if (timeline != nullptr) {
                timeline->stop();
            }
            return ExecutionResult::Success("PLAYBACK_STOPPED", {{"state", "stopped"}});
        }
        if (sub == "set-tempo") {
            std::string_view bpmStr = args.getOption("--bpm", args.getOption("--tempo", "120.0"));
            double bpm = ParsedArgs::parseDouble(bpmStr).value_or(120.0);
            if (timeline != nullptr) {
                timeline->setBPM(bpm);
            }
            std::stringstream ssBpm;
            ssBpm << std::fixed << std::setprecision(2) << bpm;
            return ExecutionResult::Success("TEMPO_UPDATED", {{"tempo", ssBpm.str()}});
        }
        if (sub == "set-time-signature") {
            std::string_view numStr = args.getOption("--num", "4");
            std::string_view denStr = args.getOption("--den", "4");
            uint8_t num = static_cast<uint8_t>(ParsedArgs::parseUint32(numStr).value_or(4));
            uint8_t den = static_cast<uint8_t>(ParsedArgs::parseUint32(denStr).value_or(4));
            if (timeline != nullptr) {
                timeline->setTimeSignature({num, den});
            }
            return ExecutionResult::Success("TIME_SIGNATURE_UPDATED", {{"time_signature", std::string(numStr) + "/" + std::string(denStr)}});
        }
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Unknown transport subcommand or flag.");
}

} // namespace agentic
