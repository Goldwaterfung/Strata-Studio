#pragma once

#include "../../common/ipc_protocol.h"
#include "../../common/parsed_args.h"

namespace bridge {
class IAnalysisController;
}

namespace agentic {

class AnalysisHandler {
public:
    static ExecutionResult handleCommand(const ParsedArgs& args, bridge::IAnalysisController* controller = nullptr);
};

} // namespace agentic
