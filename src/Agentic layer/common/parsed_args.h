#pragma once

#include "ipc_protocol.h"
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace agentic {

class ParsedArgs {
public:
    ParsedArgs() = default;

    // Parse from single raw command line string (IPC server)
    static ParsedArgs parseCommandLine(std::string_view commandLine);

    // Parse from argc/argv span (Client)
    static ParsedArgs parseArgv(std::span<char*> args);

    [[nodiscard]] std::string_view getVerb() const noexcept { return m_verb; }
    [[nodiscard]] std::string_view getSubcommand() const noexcept { return m_subcommand; }

    [[nodiscard]] bool hasFlag(std::string_view flagName) const noexcept;
    [[nodiscard]] std::string_view getOption(std::string_view optionName, std::string_view defaultValue = "") const noexcept;

    [[nodiscard]] OutputFormat getFormat() const noexcept;

    // Exception-safe numeric conversion helpers
    [[nodiscard]] static std::optional<float> parseFloat(std::string_view str) noexcept;
    [[nodiscard]] static std::optional<double> parseDouble(std::string_view str) noexcept;
    [[nodiscard]] static std::optional<uint32_t> parseUint32(std::string_view str) noexcept;
    [[nodiscard]] static std::optional<int32_t> parseInt32(std::string_view str) noexcept;

    // Helper to parse track range syntax: "1", "1..4", "1..4,7..10"
    [[nodiscard]] static std::vector<uint32_t> parseIntegerRange(std::string_view rangeStr);

private:
    std::string m_verb{};
    std::string m_subcommand{};
    std::map<std::string, std::string> m_options{};
    std::vector<std::string> m_positionals{};
};

} // namespace agentic

