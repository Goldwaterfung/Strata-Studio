#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace agentic {

// Default UNIX Domain Socket path
constexpr std::string_view DEFAULT_SOCKET_PATH = "/tmp/daw_session.sock";

// System Error Code Registry
namespace ErrorCode {
    constexpr int OK                      = 0;  // Command executed cleanly
    constexpr int INVALID_ARGS            = 70; // Grammar error or missing flag
    constexpr int DAW_NOT_RUNNING         = 71; // No active socket connection
    constexpr int ENTITY_NOT_FOUND        = 72; // Target ID, Clip, Plugin, or Marker missing
    constexpr int ENGINE_PLAYING_LOCKED   = 73; // Structural mutation during active transport
    constexpr int RESOURCE_BUSY_USER_TOUCH = 74; // Parameter locked due to active GUI drag
    constexpr int PLUGIN_FAULT            = 75; // VST3/AU failed or parameter out of bounds
    constexpr int ASSET_I_O_ERROR         = 76; // Audio clip path unreadable or invalid format
}

// Machine-Parsable Output Format Taxonomy
enum class OutputFormat {
    TSV,    // Tab-Separated Values (Default for list queries)
    KV,     // Key-Value pairs (Default for inspect queries)
    JSON,   // Compact JSON
    PRETTY  // Indented human-readable format
};

// Execution Result Container
struct ExecutionResult {
    int code{ErrorCode::OK};
    std::string symbol{"OK"};
    std::string message{};
    std::map<std::string, std::string> fields{};
    std::vector<std::map<std::string, std::string>> rows{};

    [[nodiscard]] bool isSuccess() const noexcept {
        return code == ErrorCode::OK;
    }

    static ExecutionResult Success(std::string symbol = "OK",
                                   std::map<std::string, std::string> fields = {}) {
        return ExecutionResult{ErrorCode::OK, std::move(symbol), "", std::move(fields), {}};
    }

    static ExecutionResult MultiSuccess(std::string symbol,
                                        std::vector<std::map<std::string, std::string>> rows) {
        return ExecutionResult{ErrorCode::OK, std::move(symbol), "", {}, std::move(rows)};
    }

    static ExecutionResult Error(int code, std::string symbol, std::string message) {
        return ExecutionResult{code, std::move(symbol), std::move(message), {}, {}};
    }
};

} // namespace agentic
