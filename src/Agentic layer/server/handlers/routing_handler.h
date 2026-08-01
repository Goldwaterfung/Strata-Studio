#pragma once

#include "../../common/ipc_protocol.h"
#include "../../common/parsed_args.h"

namespace agentic {

class RoutingHandler {
public:
    static ExecutionResult handleCommand(const ParsedArgs& args);
};

} // namespace agentic
