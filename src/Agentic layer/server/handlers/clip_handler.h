#pragma once

#include "../../common/ipc_protocol.h"
#include "../../common/parsed_args.h"

namespace bridge {
class IArrangementController;
class IMidiEditorController;
class ITimelineController;
} // namespace bridge

namespace agentic {

class ClipHandler {
public:
    static ExecutionResult handleCommand(
        const ParsedArgs& args,
        bridge::IArrangementController* arrangementController = nullptr,
        bridge::IMidiEditorController* midiEditorController = nullptr,
        bridge::ITimelineController* timelineController = nullptr
    );
};

} // namespace agentic
