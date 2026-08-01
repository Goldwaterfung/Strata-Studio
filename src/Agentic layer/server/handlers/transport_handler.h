#pragma once

#include "../../common/ipc_protocol.h"
#include "../../common/parsed_args.h"

namespace bridge {
class ITimelineController;
}

namespace agentic {

class TransportHandler {
public:
    static ExecutionResult handleCommand(const ParsedArgs& args, bridge::ITimelineController* timelineController = nullptr);
};

} // namespace agentic
