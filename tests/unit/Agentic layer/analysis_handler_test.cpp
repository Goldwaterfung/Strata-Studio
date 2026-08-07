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

    SECTION("Executes dynamically when controller is injected") {
        bridge::AnalysisController controller;
        agentic::ParsedArgs args = agentic::ParsedArgs::parseCommandLine("analyze spectrum --track 1");
        auto res = agentic::AnalysisHandler::handleCommand(args, &controller);
        REQUIRE(res.isSuccess());
        REQUIRE(res.fields.at("TRACK_ID") == "1");
        REQUIRE(res.fields.count("SPECTRAL_CENTROID_HZ") == 1);
    }
}
