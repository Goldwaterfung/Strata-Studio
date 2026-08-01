#pragma once

#include "../common/ipc_protocol.h"
#include <string>

namespace agentic {

class ResultFormatter {
public:
    static std::string formatResult(const ExecutionResult& result, OutputFormat format);
};

} // namespace agentic
