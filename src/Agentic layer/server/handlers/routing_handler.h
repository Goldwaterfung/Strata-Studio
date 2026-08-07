#pragma once

#include "../../common/ipc_protocol.h"
#include "../../common/parsed_args.h"

namespace bridge {
class ITrackController;
}

namespace agentic {

class RoutingHandler {
public:
    static ExecutionResult handleCommand(const ParsedArgs& args, bridge::ITrackController* trackController = nullptr);
};

} // namespace agentic
