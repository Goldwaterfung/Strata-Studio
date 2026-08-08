#include "command_dispatcher.h"
#include "result_formatter.h"
#include "handlers/analysis_handler.h"
#include "handlers/clip_handler.h"
#include "handlers/plugin_handler.h"
#include "handlers/rendering_handler.h"
#include "handlers/routing_handler.h"
#include "handlers/track_handler.h"
#include "handlers/transport_handler.h"

namespace agentic {

CommandDispatcher::CommandDispatcher() {
    registerHandlers();
}

CommandDispatcher::CommandDispatcher(BridgeControllers controllers)
    : m_controllers(controllers) {
    registerHandlers();
}

void CommandDispatcher::setControllers(BridgeControllers controllers) {
    m_controllers = controllers;
    registerHandlers();
}

void CommandDispatcher::registerHandlers() {
    m_verbHandlers["status"]    = [this](const ParsedArgs& args) { return TransportHandler::handleCommand(args, m_controllers.timelineController); };
    m_verbHandlers["transport"] = [this](const ParsedArgs& args) { return TransportHandler::handleCommand(args, m_controllers.timelineController); };
    m_verbHandlers["track"]     = [this](const ParsedArgs& args) { return TrackHandler::handleCommand(args, m_controllers.trackController); };
    m_verbHandlers["prep"]      = [this](const ParsedArgs& args) { return TrackHandler::handleCommand(args, m_controllers.trackController); };
    m_verbHandlers["plugin"]    = [this](const ParsedArgs& args) { return PluginHandler::handleCommand(args, m_controllers.trackController, m_controllers.browserController); };
    m_verbHandlers["clip"]      = [this](const ParsedArgs& args) { return ClipHandler::handleCommand(args, m_controllers.arrangementController, m_controllers.midiEditorController, m_controllers.timelineController); };
    m_verbHandlers["midi"]      = [this](const ParsedArgs& args) { return ClipHandler::handleCommand(args, m_controllers.arrangementController, m_controllers.midiEditorController, m_controllers.timelineController); };
    m_verbHandlers["route"]     = [this](const ParsedArgs& args) { return RoutingHandler::handleCommand(args, m_controllers.trackController); };
    m_verbHandlers["analyze"]   = [this](const ParsedArgs& args) { return AnalysisHandler::handleCommand(args, m_controllers.analysisController); };
    m_verbHandlers["export"]    = [](const ParsedArgs& args) { return RenderingHandler::handleCommand(args); };
    m_verbHandlers["job"]       = [](const ParsedArgs& args) { return RenderingHandler::handleCommand(args); };
}

ExecutionResult CommandDispatcher::dispatch(const ParsedArgs& args) {
    std::string verb(args.getVerb());
    if (verb.empty()) {
        return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Empty command string.");
    }

    auto it = m_verbHandlers.find(verb);
    if (it != m_verbHandlers.end()) {
        try {
            return it->second(args);
        } catch (const std::exception& e) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, e.what());
        } catch (...) {
            return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Unknown exception during command execution.");
        }
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "Command verb '" + verb + "' is not recognized.");
}

std::string CommandDispatcher::executeCommand(std::string_view commandLine) {
    ParsedArgs args = ParsedArgs::parseCommandLine(commandLine);
    ExecutionResult result = dispatch(args);
    return ResultFormatter::formatResult(result, args.getFormat());
}

} // namespace agentic
