#pragma once

#include "../../common/ipc_protocol.h"
#include "../../common/parsed_args.h"

namespace agentic {

class RenderingHandler {
public:
    static ExecutionResult handleCommand(const ParsedArgs& args);
};

} // namespace agentic
