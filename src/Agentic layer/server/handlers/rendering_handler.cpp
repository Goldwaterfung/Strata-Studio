#include "rendering_handler.h"

namespace agentic {

ExecutionResult RenderingHandler::handleCommand(const ParsedArgs& args) {
    std::string_view verb = args.getVerb();
    std::string_view sub = args.getSubcommand();

    if (verb == "export" && sub == "stems") {
        std::string_view out = args.getOption("--output", "/exports/stems/");
        return ExecutionResult::Success("JOB_STARTED", {
            {"job_id", "201"},
            {"total_tracks", "40"},
            {"destination", std::string(out)}
        });
    }

    if (verb == "job") {
        if (sub == "status") {
            std::string_view jobId = args.getOption("--id", "201");
            return ExecutionResult::Success("JOB_STATUS", {
                {"job_id", std::string(jobId)},
                {"status", "COMPLETED"},
                {"progress", "100%"},
                {"files_rendered", "40"}
            });
        }
        if (sub == "cancel") {
            std::string_view jobId = args.getOption("--id", "201");
            return ExecutionResult::Success("JOB_CANCELLED", {
                {"job_id", std::string(jobId)}
            });
        }
        if (sub == "list") {
            return ExecutionResult::Success("JOB_LIST", {
                {"JOB_ID", "201"},
                {"TYPE", "STEM_EXPORT"},
                {"PROGRESS", "100%"},
                {"STATUS", "COMPLETED"}
            });
        }
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Unknown export/job subcommand.");
}

} // namespace agentic
