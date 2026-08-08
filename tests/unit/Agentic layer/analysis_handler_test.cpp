#include <catch2/catch_test_macros.hpp>
#include "Agentic layer/server/handlers/analysis_handler.h"
#include "Agentic layer/common/parsed_args.h"
#include "Middle Bridge/analysis/analysis_controller.h"

TEST_CASE("agentic::AnalysisHandler Strict Non-Fallback Audit", "[agentic][analysis]") {
    SECTION("Fails cleanly with ENTITY_NOT_FOUND when controller is null (no fallbacks allowed)") {
        agentic::ParsedArgs args = agentic::ParsedArgs::parseCommandLine("analyze masking --track 1 --vs 2");
        auto res = agentic::AnalysisHandler::handleCommand(args, nullptr);
        REQUIRE_FALSE(res.isSuccess());
        REQUIRE(res.code == agentic::ErrorCode::ENTITY_NOT_FOUND);
        REQUIRE(res.symbol == "CONTROLLER_UNAVAILABLE");
    }

    SECTION("Fails cleanly with INVALID_ARGS when required --track option is missing") {
        bridge::AnalysisController controller;
        agentic::ParsedArgs args = agentic::ParsedArgs::parseCommandLine("analyze spectrum");
        auto res = agentic::AnalysisHandler::handleCommand(args, &controller);
        REQUIRE_FALSE(res.isSuccess());
        REQUIRE(res.code == agentic::ErrorCode::INVALID_ARGS);
        REQUIRE(res.symbol == "INVALID_ARGS");
    }

    SECTION("Fails cleanly with INVALID_ARGS when window start or duration is omitted") {
        bridge::AnalysisController controller;
        agentic::ParsedArgs args = agentic::ParsedArgs::parseCommandLine("analyze window --track 1");
        auto res = agentic::AnalysisHandler::handleCommand(args, &controller);
        REQUIRE_FALSE(res.isSuccess());
        REQUIRE(res.code == agentic::ErrorCode::INVALID_ARGS);
        REQUIRE(res.symbol == "INVALID_ARGS");
    }

    SECTION("Fails cleanly with UNKNOWN_SUBCOMMAND when unknown analysis type requested") {
        bridge::AnalysisController controller;
        agentic::ParsedArgs args = agentic::ParsedArgs::parseCommandLine("analyze unknown_type --track 1");
        auto res = agentic::AnalysisHandler::handleCommand(args, &controller);
        REQUIRE_FALSE(res.isSuccess());
        REQUIRE(res.code == agentic::ErrorCode::INVALID_ARGS);
        REQUIRE(res.symbol == "UNKNOWN_SUBCOMMAND");
    }
}
