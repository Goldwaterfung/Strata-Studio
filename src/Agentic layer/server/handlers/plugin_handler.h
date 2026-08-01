#pragma once

#include "../../common/ipc_protocol.h"
#include "../../common/parsed_args.h"

namespace bridge {
class ITrackController;
class IBrowserController;
}

namespace agentic {

class PluginHandler {
public:
    static ExecutionResult handleCommand(const ParsedArgs& args,
                                         bridge::ITrackController* trackController = nullptr,
                                         bridge::IBrowserController* browserController = nullptr);
};

} // namespace agentic
