#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/tracks/track_controller.h"
#include "mocks/mock_track_environment.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "DSP nodes/plugins/insert_plugin_node.h"
#include "Core audio engine/plugin/iplugin.h"

TEST_CASE("TrackController: UI State Queries and Multi-Track Sorting", "[MiddleBridge][TrackController]") {
    auto stringRegistry = Layer2::IStringRegistry::create();
    auto mutationBridge = Layer2::IMutationBridge::create(128);
    MockTrackManager trackManager;
    MockMeteringProvider meteringProvider;
    MockSessionManager sessionManager;
    auto session = std::make_unique<MockProjectSession>(&trackManager);
    sessionManager.setActiveSession(std::move(session));

    bridge::TrackController controller(
        &sessionManager,
        mutationBridge.get(),
        stringRegistry.get(),
        &meteringProvider
    );

    // Create 3 tracks
    TrackID trackA = controller.addAudioTrack("A", 2, 0xFF111111);
    TrackID trackB = controller.addAudioTrack("B", 2, 0xFF222222);
    TrackID trackC = controller.addAudioTrack("C", 2, 0xFF333333);

    // Artificially change their positions
    trackManager.moveTrack(trackA, 2, {0, 0});
    trackManager.moveTrack(trackB, 0, {0, 0});
    trackManager.moveTrack(trackC, 1, {0, 0});

    // Mock telemetry levels
    meteringProvider.levels[trackA.toRaw()] = bridge::MeterLevel{-5.0f, -5.2f, -10.0f, -10.2f};
    meteringProvider.levels[trackB.toRaw()] = bridge::MeterLevel{-12.0f, -12.1f, -18.0f, -18.1f};
    meteringProvider.levels[trackC.toRaw()] = bridge::MeterLevel{-0.1f, -0.2f, -3.0f, -3.1f};

    SECTION("Get Individual Track UI State") {
        auto state = controller.getTrackState(trackA);
        CHECK(std::string(state.name) == "A");
        CHECK(state.meterLeftPeak == -5.0f);
        CHECK(state.meterRightPeak == -5.2f);
    }

    SECTION("Get All Tracks Sorted by Index Position") {
        auto tracksList = controller.getAllTracks();
        REQUIRE(tracksList.size() == 3);

        // Should be sorted: B (position 0), C (position 1), A (position 2)
        CHECK(std::string(tracksList[0].name) == "B");
        CHECK(std::string(tracksList[1].name) == "C");
        CHECK(std::string(tracksList[2].name) == "A");
    }

    SECTION("Scan Plugin and Instrument Parameters in Cache") {
        // Create mock synth instrument slot node
        NodeID instNode = DSP::InstrumentSlotFactory::getRegistry().allocate().value_or(NodeID::invalid());
        REQUIRE(instNode.isValid());
        auto* instState = DSP::InstrumentSlotFactory::getRegistry().get(instNode);
        REQUIRE(instState != nullptr);
        std::strcpy(instState->name, "SuperSynth");
        
        // Define a mock plugin
        class MockPlugin : public Layer3::IPlugin {
        public:
            bool getInfo(PluginInfo &outInfo) const override {
                outInfo.numInputs = 2;
                outInfo.numOutputs = 2;
                outInfo.numParameters = 2;
                outInfo.latencySamples = 0;
                outInfo.hasEditor = false;
                outInfo.isInstrument = true;
                std::strcpy(outInfo.name, "SuperSynthPlugin");
                return true;
            }
            void processAudio(float *const *, uint32_t, float *const *, uint32_t, uint32_t, const EventData *, uint32_t, EventData *, uint32_t *, const ProcessContext *, const bool*) override {}
            float getParameterValue(uint32_t) const override { return 0.5f; }
            void setParameterValue(uint32_t, float) override {}
            bool getParameterInfo(uint32_t paramIndex, ::ParameterInfo &outInfo) const override {
                outInfo.index = paramIndex;
                outInfo.flags = ::ParameterInfo::IS_AUTOMATABLE;
                if (paramIndex == 0) {
                    std::strcpy(outInfo.name, "Cutoff");
                    outInfo.minValue = 20.0f;
                    outInfo.maxValue = 20000.0f;
                    outInfo.defaultValue = 1000.0f;
                    return true;
                } else if (paramIndex == 1) {
                    std::strcpy(outInfo.name, "Resonance");
                    outInfo.minValue = 0.0f;
                    outInfo.maxValue = 1.0f;
                    outInfo.defaultValue = 0.1f;
                    return true;
                }
                return false;
            }
            void setParameterTweakedCallback(ParameterTweakedCallback /*cb*/) override {}
            std::vector<uint8_t> getState() const override { return {}; }
            bool loadState(const uint8_t *, uint64_t) override { return true; }
            bool openEditor(void *, int &, int &) override { return true; }
            void closeEditor() override {}
        };
        MockPlugin mockSynth;
        instState->pluginInstance = &mockSynth;

        // Create mock Plugin Slot (effects head)
        NodeID slotNodeId = DSP::PluginSlotFactory::getRegistry().allocate().value_or(NodeID::invalid());
        REQUIRE(slotNodeId.isValid());
        auto* slotState = DSP::PluginSlotFactory::getRegistry().get(slotNodeId);
        REQUIRE(slotState != nullptr);
        
        // Allocate Insert Plugin Node for slot 0
        NodeID fxNodeId = DSP::InsertPluginFactory::getRegistry().allocate().value_or(NodeID::invalid());
        REQUIRE(fxNodeId.isValid());
        auto* fxState = DSP::InsertPluginFactory::getRegistry().get(fxNodeId);
        REQUIRE(fxState != nullptr);
        std::strcpy(fxState->name, "DelayFX");
        
        class MockFXPlugin : public Layer3::IPlugin {
        public:
            bool getInfo(PluginInfo &outInfo) const override {
                outInfo.numInputs = 2;
                outInfo.numOutputs = 2;
                outInfo.numParameters = 1;
                outInfo.latencySamples = 0;
                outInfo.hasEditor = false;
                outInfo.isInstrument = false;
                std::strcpy(outInfo.name, "DelayPlugin");
                return true;
            }
            void processAudio(float *const *, uint32_t, float *const *, uint32_t, uint32_t, const EventData *, uint32_t, EventData *, uint32_t *, const ProcessContext *, const bool*) override {}
            float getParameterValue(uint32_t) const override { return 0.2f; }
            void setParameterValue(uint32_t, float) override {}
            bool getParameterInfo(uint32_t paramIndex, ::ParameterInfo &outInfo) const override {
                outInfo.index = paramIndex;
                outInfo.flags = ::ParameterInfo::IS_AUTOMATABLE;
                if (paramIndex == 0) {
                    std::strcpy(outInfo.name, "DelayTime");
                    outInfo.minValue = 0.0f;
                    outInfo.maxValue = 2.0f;
                    outInfo.defaultValue = 0.5f;
                    return true;
                }
                return false;
            }
            void setParameterTweakedCallback(ParameterTweakedCallback /*cb*/) override {}
            std::vector<uint8_t> getState() const override { return {}; }
            bool loadState(const uint8_t *, uint64_t) override { return true; }
            bool openEditor(void *, int &, int &) override { return true; }
            void closeEditor() override {}
        };
        MockFXPlugin mockFX;
        fxState->pluginInstance = &mockFX;
        slotState->slots[0] = fxNodeId;

        // Assign these to Track A's pipeline descriptor
        trackManager.tracks[trackA.id].pipeline.instrumentSlotNode = instNode;
        auto trackNodeId = trackManager.tracks[trackA.id].pipeline.trackNode;
        if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(trackNodeId)) {
            trk->pluginSlot = *slotState;
        }

        // Manually trigger onSessionChanged to bind routing callback and run cache initialization
        controller.onSessionChanged(sessionManager.getActiveSession());
        
        // Trigger parameter cache rebuild
        REQUIRE(trackManager.mixerRoutingCallback != nullptr);
        trackManager.mixerRoutingCallback(trackA);

        // Fetch cached parameters
        auto params = controller.getCachedParameters(trackA);
        
        // Let's verify our parameters are found and formatted correctly
        bool foundInstBypass = false;
        bool foundCutoff = false;
        bool foundResonance = false;
        bool foundFXBypass = false;
        bool foundDelayTime = false;

        for (const auto& item : params) {
            std::string name = item.info.name;
            if (item.routingNodeId == instNode) {
                if (item.parameterIndex == ::BYPASS_PARAMETER_INDEX) {
                    foundInstBypass = (name == "SuperSynth Bypass");
                } else if (item.parameterIndex == 0) {
                    foundCutoff = (name == "SuperSynth - Cutoff");
                } else if (item.parameterIndex == 1) {
                    foundResonance = (name == "SuperSynth - Resonance");
                }
            } else if (item.routingNodeId == trackNodeId && item.subNodeId == fxNodeId.id) {
                if (item.parameterIndex == ::BYPASS_PARAMETER_INDEX) {
                    foundFXBypass = (name == "DelayFX Bypass");
                } else if (item.parameterIndex == 0) {
                    foundDelayTime = (name == "DelayFX - DelayTime");
                }
            }
        }

        CHECK(foundInstBypass);
        CHECK(foundCutoff);
        CHECK(foundResonance);
        CHECK(foundFXBypass);
        CHECK(foundDelayTime);

        // Cleanup registries
        instState->pluginInstance = nullptr;
        fxState->pluginInstance = nullptr;
        DSP::InstrumentSlotFactory::getRegistry().deallocate(instNode);
        DSP::InsertPluginFactory::getRegistry().deallocate(fxNodeId);
        DSP::PluginSlotFactory::getRegistry().deallocate(slotNodeId);
    }
}

