#include <catch2/catch_test_macros.hpp>
#include "Core infrastructure/plugins/plugin_validator.h"

using namespace Layer2;

TEST_CASE("Plugin Category Classification Accuracy", "[Layer2][Plugin]") {
    SECTION("Instrument Classification by Explicit Flag") {
        // When explicit isInstrument flag is set, it must always be categorized as an Instrument
        uint8_t cat = PluginValidator::classifyPluginByVST3Category("Equalizer", "", true);
        CHECK(cat == PluginCategory::INSTRUMENT);
    }

    SECTION("Instrument Classification by Category Matching") {
        // VST3 subcategory checks for instrument
        CHECK(PluginValidator::classifyPluginByVST3Category("Instrument", "", false) == PluginCategory::INSTRUMENT);
        CHECK(PluginValidator::classifyPluginByVST3Category("Instrument|Synth", "", false) == PluginCategory::INSTRUMENT);
        CHECK(PluginValidator::classifyPluginByVST3Category("Synth", "Instrument", false) == PluginCategory::INSTRUMENT);
    }

    SECTION("Delay / Reverb Classification") {
        CHECK(PluginValidator::classifyPluginByVST3Category("Delay", "Fx", false) == PluginCategory::EFFECT_DELAY_REVERB);
        CHECK(PluginValidator::classifyPluginByVST3Category("Reverb", "Fx", false) == PluginCategory::EFFECT_DELAY_REVERB);
        CHECK(PluginValidator::classifyPluginByVST3Category("Fx|Delay", "", false) == PluginCategory::EFFECT_DELAY_REVERB);
    }

    SECTION("Distortion Classification") {
        CHECK(PluginValidator::classifyPluginByVST3Category("Distortion", "Fx", false) == PluginCategory::EFFECT_DISTORTION);
        CHECK(PluginValidator::classifyPluginByVST3Category("Guitar", "Fx", false) == PluginCategory::EFFECT_DISTORTION);
        CHECK(PluginValidator::classifyPluginByVST3Category("Bass", "Fx", false) == PluginCategory::EFFECT_DISTORTION);
    }

    SECTION("Dynamics Classification") {
        CHECK(PluginValidator::classifyPluginByVST3Category("Dynamics", "Fx", false) == PluginCategory::EFFECT_DYNAMICS);
        CHECK(PluginValidator::classifyPluginByVST3Category("Fx|Dynamics", "", false) == PluginCategory::EFFECT_DYNAMICS);
    }

    SECTION("EQ / Filter Classification") {
        CHECK(PluginValidator::classifyPluginByVST3Category("EQ", "Fx", false) == PluginCategory::EFFECT_EQ_FILTER);
        CHECK(PluginValidator::classifyPluginByVST3Category("Filter", "Fx", false) == PluginCategory::EFFECT_EQ_FILTER);
        CHECK(PluginValidator::classifyPluginByVST3Category("EQ|Filter", "", false) == PluginCategory::EFFECT_EQ_FILTER);
    }

    SECTION("Modulation Classification") {
        CHECK(PluginValidator::classifyPluginByVST3Category("Modulation", "Fx", false) == PluginCategory::EFFECT_MODULATION);
        CHECK(PluginValidator::classifyPluginByVST3Category("Fx|Modulation", "", false) == PluginCategory::EFFECT_MODULATION);
    }

    SECTION("Other / Unclassified Effects") {
        CHECK(PluginValidator::classifyPluginByVST3Category("Spatial", "Fx", false) == PluginCategory::EFFECT_OTHER);
        CHECK(PluginValidator::classifyPluginByVST3Category("Analyzer", "Fx", false) == PluginCategory::EFFECT_OTHER);
    }
}
