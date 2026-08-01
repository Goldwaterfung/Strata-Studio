#include "result_formatter.h"
#include <sstream>

namespace agentic {

std::string ResultFormatter::formatResult(const ExecutionResult& result, OutputFormat format) {
    if (!result.isSuccess()) {
        std::stringstream ss;
        ss << "ERROR " << result.code << " " << result.symbol << " \"" << result.message << "\"\n";
        return ss.str();
    }

    if (result.fields.empty() && result.rows.empty()) {
        std::stringstream ss;
        ss << "OK " << result.symbol << "\n";
        return ss.str();
    }

    std::stringstream ss;

    if (!result.rows.empty()) {
        switch (format) {
            case OutputFormat::TSV: {
                // Header line from first row
                const auto& firstRow = result.rows.front();
                bool firstKey = true;
                for (const auto& [key, val] : firstRow) {
                    if (!firstKey) ss << "\t";
                    ss << key;
                    firstKey = false;
                }
                ss << "\n";

                // Each data row
                for (const auto& row : result.rows) {
                    bool firstVal = true;
                    for (const auto& [key, val] : row) {
                        if (!firstVal) ss << "\t";
                        ss << val;
                        firstVal = false;
                    }
                    ss << "\n";
                }
                break;
            }

            case OutputFormat::KV: {
                std::size_t rowIndex = 0;
                for (const auto& row : result.rows) {
                    ss << "--- Track Record [" << (rowIndex++) << "] ---\n";
                    for (const auto& [key, val] : row) {
                        ss << key << ": " << val << "\n";
                    }
                }
                break;
            }

            case OutputFormat::JSON: {
                ss << "[";
                bool firstRow = true;
                for (const auto& row : result.rows) {
                    if (!firstRow) ss << ",";
                    ss << "{";
                    bool firstVal = true;
                    for (const auto& [key, val] : row) {
                        if (!firstVal) ss << ",";
                        ss << "\"" << key << "\":\"" << val << "\"";
                        firstVal = false;
                    }
                    ss << "}";
                    firstRow = false;
                }
                ss << "]\n";
                break;
            }

            case OutputFormat::PRETTY: {
                ss << "OK " << result.symbol << " (" << result.rows.size() << " items):\n";
                std::size_t rowIndex = 0;
                for (const auto& row : result.rows) {
                    ss << "  Item #" << (rowIndex++) << ":\n";
                    for (const auto& [key, val] : row) {
                        ss << "    " << key << " = " << val << "\n";
                    }
                }
                break;
            }
        }
        return ss.str();
    }

    switch (format) {
        case OutputFormat::TSV: {
            // Header line
            bool first = true;
            for (const auto& [key, val] : result.fields) {
                if (!first) ss << "\t";
                ss << key;
                first = false;
            }
            ss << "\n";
            // Value line
            first = true;
            for (const auto& [key, val] : result.fields) {
                if (!first) ss << "\t";
                ss << val;
                first = false;
            }
            ss << "\n";
            break;
        }

        case OutputFormat::KV: {
            for (const auto& [key, val] : result.fields) {
                ss << key << ": " << val << "\n";
            }
            break;
        }

        case OutputFormat::JSON: {
            ss << "{";
            bool first = true;
            for (const auto& [key, val] : result.fields) {
                if (!first) ss << ",";
                ss << "\"" << key << "\":\"" << val << "\"";
                first = false;
            }
            ss << "}\n";
            break;
        }

        case OutputFormat::PRETTY: {
            ss << "OK " << result.symbol << ":\n";
            for (const auto& [key, val] : result.fields) {
                ss << "  " << key << " = " << val << "\n";
            }
            break;
        }
    }

    return ss.str();
}

} // namespace agentic
