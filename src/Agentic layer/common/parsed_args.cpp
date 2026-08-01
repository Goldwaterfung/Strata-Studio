#include "parsed_args.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace agentic {

namespace {

[[nodiscard]] bool isOptionToken(std::string_view tok) noexcept {
    if (tok.rfind("--", 0) == 0 && tok.size() > 2) {
        return true;
    }
    if (tok.size() >= 2 && tok[0] == '-' && std::isalpha(static_cast<unsigned char>(tok[1]))) {
        return true;
    }
    return false;
}

} // namespace

ParsedArgs ParsedArgs::parseCommandLine(std::string_view commandLine) {
    ParsedArgs result;
    std::string_view sv = commandLine;

    // Trim trailing newlines and whitespace
    while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' || sv.back() == ' ')) {
        sv.remove_suffix(1);
    }

    if (sv.empty()) {
        return result;
    }

    // Tokenize string splitting by space (respecting double quotes)
    std::vector<std::string> tokens;
    std::string currentToken;
    bool inQuotes = false;

    for (char c : sv) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ' ' && !inQuotes) {
            if (!currentToken.empty()) {
                tokens.push_back(currentToken);
                currentToken.clear();
            }
        } else {
            currentToken.push_back(c);
        }
    }
    if (!currentToken.empty()) {
        tokens.push_back(currentToken);
    }

    if (tokens.empty()) {
        return result;
    }

    result.m_verb = tokens[0];
    std::size_t idx = 1;

    if (tokens.size() > 1 && !tokens[1].empty() && !isOptionToken(tokens[1])) {
        result.m_subcommand = tokens[1];
        idx = 2;
    }

    for (; idx < tokens.size(); ++idx) {
        const auto& tok = tokens[idx];
        if (isOptionToken(tok)) {
            std::string key = tok;
            if (idx + 1 < tokens.size() && !isOptionToken(tokens[idx + 1])) {
                result.m_options[key] = tokens[idx + 1];
                ++idx;
            } else {
                result.m_options[key] = "true";
            }
        } else {
            result.m_positionals.push_back(tok);
        }
    }

    return result;
}

ParsedArgs ParsedArgs::parseArgv(std::span<char*> args) {
    std::string cmd{};
    for (const char* arg : args) {
        if (arg == nullptr) continue;
        if (std::strchr(arg, ' ') != nullptr) {
            cmd.append("\"").append(arg).append("\" ");
        } else {
            cmd.append(arg).append(" ");
        }
    }
    return parseCommandLine(cmd);
}

bool ParsedArgs::hasFlag(std::string_view flagName) const noexcept {
    std::string key(flagName);
    if (key.rfind("--", 0) != 0) {
        key = "--" + key;
    }
    auto it = m_options.find(key);
    if (it == m_options.end()) return false;
    return it->second != "false" && it->second != "0";
}

std::string_view ParsedArgs::getOption(std::string_view optionName, std::string_view defaultValue) const noexcept {
    std::string key(optionName);
    if (key.rfind("--", 0) != 0) {
        key = "--" + key;
    }
    auto it = m_options.find(key);
    if (it != m_options.end()) {
        return it->second;
    }
    return defaultValue;
}

OutputFormat ParsedArgs::getFormat() const noexcept {
    std::string_view fmt = getOption("--format", "");
    if (fmt.empty()) {
        const char* envFmt = std::getenv("DAW_CLI_DEFAULT_FORMAT");
        if (envFmt != nullptr) {
            fmt = envFmt;
        }
    }

    if (fmt == "tsv") return OutputFormat::TSV;
    if (fmt == "kv") return OutputFormat::KV;
    if (fmt == "json") return OutputFormat::JSON;
    if (fmt == "pretty") return OutputFormat::PRETTY;

    return OutputFormat::TSV; // Default format
}

std::optional<float> ParsedArgs::parseFloat(std::string_view str) noexcept {
    if (str.empty()) return std::nullopt;
    try {
        std::size_t pos = 0;
        std::string s(str);
        float val = std::stof(s, &pos);
        if (pos == s.size()) {
            return val;
        }
    } catch (...) {}
    return std::nullopt;
}

std::optional<double> ParsedArgs::parseDouble(std::string_view str) noexcept {
    if (str.empty()) return std::nullopt;
    try {
        std::size_t pos = 0;
        std::string s(str);
        double val = std::stod(s, &pos);
        if (pos == s.size()) {
            return val;
        }
    } catch (...) {}
    return std::nullopt;
}

std::optional<uint32_t> ParsedArgs::parseUint32(std::string_view str) noexcept {
    if (str.empty()) return std::nullopt;
    try {
        std::size_t pos = 0;
        std::string s(str);
        unsigned long val = std::stoul(s, &pos);
        if (pos == s.size()) {
            return static_cast<uint32_t>(val);
        }
    } catch (...) {}
    return std::nullopt;
}

std::optional<int32_t> ParsedArgs::parseInt32(std::string_view str) noexcept {
    if (str.empty()) return std::nullopt;
    try {
        std::size_t pos = 0;
        std::string s(str);
        long val = std::stol(s, &pos);
        if (pos == s.size()) {
            return static_cast<int32_t>(val);
        }
    } catch (...) {}
    return std::nullopt;
}

std::vector<uint32_t> ParsedArgs::parseIntegerRange(std::string_view rangeStr) {
    std::vector<uint32_t> result;
    if (rangeStr.empty()) return result;

    const std::string input(rangeStr);
    std::stringstream ss(input);
    std::string segment;

    while (std::getline(ss, segment, ',')) {
        if (segment.empty()) continue;
        auto dots = segment.find("..");
        if (dots != std::string::npos) {
            auto startOpt = parseUint32(segment.substr(0, dots));
            auto endOpt = parseUint32(segment.substr(dots + 2));
            if (startOpt.has_value() && endOpt.has_value() && *startOpt <= *endOpt) {
                for (uint32_t i = *startOpt; i <= *endOpt; ++i) {
                    result.push_back(i);
                }
            }
        } else {
            auto valOpt = parseUint32(segment);
            if (valOpt.has_value()) {
                result.push_back(*valOpt);
            }
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace agentic

