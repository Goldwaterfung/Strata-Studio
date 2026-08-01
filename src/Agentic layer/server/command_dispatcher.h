#pragma once

#include "../common/bridge_controllers.h"
#include "../common/ipc_protocol.h"
#include "../common/parsed_args.h"
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace agentic {

class CommandDispatcher {
public:
    CommandDispatcher();
    explicit CommandDispatcher(BridgeControllers controllers);

    void setControllers(BridgeControllers controllers);
    [[nodiscard]] const BridgeControllers& getControllers() const noexcept { return m_controllers; }

    // Execute parsed raw line string and return formatted text payload
    std::string executeCommand(std::string_view commandLine);

    // Direct execution returning structured ExecutionResult
    ExecutionResult dispatch(const ParsedArgs& args);

private:
    void registerHandlers();

    BridgeControllers m_controllers{};
    using HandlerFunc = std::function<ExecutionResult(const ParsedArgs&)>;
    std::map<std::string, HandlerFunc> m_verbHandlers{};
};

} // namespace agentic
