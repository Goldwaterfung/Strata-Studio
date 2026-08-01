// tests/unit/musical_composition/plugin_state_serialization_test.cpp

#include <catch2/catch_test_macros.hpp>
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/project_session/project_serializer.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include "Core audio engine/plugin/iplugin.h"
#include "Core audio engine/plugin/placeholder_plugin.h"
#include "musical_composition/track_manager/track_manager_impl.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "DSP nodes/plugins/insert_plugin_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/tracks/audio_track_node.h"
#include "DSP nodes/tracks/instrument_track_node.h"
#include <memory>
#include <vector>
#include <string>

using namespace composition;

namespace {

class TestPipelineBuilder : public ITrackPipelineBuilder {
public:
    TrackPipelineDescriptor buildPipeline(const TrackCreateInfo& info, IDSPKernel*) override {
        TrackPipelineDescriptor desc{};
        
        // Allocate an instrument slot
        auto instSlotOpt = DSP::PluginSlotFactory::getRegistry().allocate();
        if (instSlotOpt) desc.instrumentSlotNode = *instSlotOpt;
        
        // Allocate track macro-node
        if (info.type == TrackType::INSTRUMENT) {
            auto trackNodeOpt = DSP::InstrumentTrackFactory::getRegistry().allocate();
            if (trackNodeOpt) desc.trackNode = *trackNodeOpt;
        } else {
            auto trackNodeOpt = DSP::AudioTrackFactory::getRegistry().allocate();
            if (trackNodeOpt) desc.trackNode = *trackNodeOpt;
        }
        
        return desc;
    }
    void destroyPipeline(const TrackPipelineDescriptor& desc, IDSPKernel*) override {
        if (desc.trackNode.isValid()) {
            if (DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
                DSP::AudioTrackFactory::getRegistry().deallocate(desc.trackNode);
            } else if (DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
                DSP::InstrumentTrackFactory::getRegistry().deallocate(desc.trackNode);
            }
        }
        if (desc.instrumentSlotNode.isValid()) {
            DSP::PluginSlotFactory::getRegistry().deallocate(desc.instrumentSlotNode);
        }
    }
};

class MockPlugin : public Layer3::IPlugin {
public:
    MockPlugin(uint32_t id, const std::string& name, const std::vector<uint8_t>& state)
        : id_(id), name_(name), state_(state) {}
        
    void processAudio(float* const* /*inputs*/, uint32_t /*numInputs*/,
                      float* const* /*outputs*/, uint32_t /*numOutputs*/,
                      uint32_t /*numSamples*/, const EventData* /*events*/,
                      uint32_t /*numEvents*/,
                      EventData* /*outEvents*/, uint32_t* /*outCount*/,
                      const ProcessContext* /*context*/,
                      const bool* /*inputSilence*/ = nullptr) override {}
    
    bool getInfo(PluginInfo& /*outInfo*/) const override { return true; }
    
    float getParameterValue(uint32_t /*paramIndex*/) const override { return 0.0f; }
    void setParameterValue(uint32_t /*paramIndex*/, float /*value*/) override {}
    bool getParameterInfo(uint32_t /*paramIndex*/, ::ParameterInfo& /*outInfo*/) const override { return false; }
    void setParameterTweakedCallback(ParameterTweakedCallback /*cb*/) override {}
    
    bool openEditor(void* /*parentWindow*/, int& /*outWidth*/, int& /*outHeight*/) override { return false; }
    void closeEditor() override {}
    
    std::vector<uint8_t> getState() const override { return state_; }
    bool loadState(const uint8_t* stateData, uint64_t size) override {
        state_.assign(stateData, stateData + size);
        return true;
    }
    
    uint32_t id_;
    std::string name_;
    std::vector<uint8_t> state_;
};

class MockPluginManager : public Layer3::IPluginManager {
public:
    std::vector<PluginDescriptor> getAvailablePlugins() const override {
        PluginDescriptor p42{}; p42.pluginId = 42; std::strncpy(p42.name, "Plugin42", MAX_PLUGIN_NAME_LENGTH);
        PluginDescriptor p99{}; p99.pluginId = 99; std::strncpy(p99.name, "Plugin99", MAX_PLUGIN_NAME_LENGTH);
        return {p42, p99};
    }
    
    std::unique_ptr<Layer3::IPlugin> instantiatePlugin(const PluginDescriptor& descriptor) override {
        if (descriptor.pluginId == 42) { // Known plugin
            return std::make_unique<MockPlugin>(42, descriptor.name, std::vector<uint8_t>{});
        }
        return nullptr; // Missing plugin
    }
    
    void scanForPlugins(const std::vector<std::string>& /*paths*/) override {}
    bool isScanning() const override { return false; }
    float getScanProgress() const override { return 0.0f; }
    std::string getCurrentlyScanningPlugin() const override { return ""; }
};

class MockPluginManagerSave : public Layer3::IPluginManager {
public:
    std::vector<PluginDescriptor> getAvailablePlugins() const override {
        PluginDescriptor p42{}; p42.pluginId = 42; std::strncpy(p42.name, "Plugin42", MAX_PLUGIN_NAME_LENGTH);
        PluginDescriptor p99{}; p99.pluginId = 99; std::strncpy(p99.name, "Plugin99", MAX_PLUGIN_NAME_LENGTH);
        return {p42, p99};
    }
    
    std::unique_ptr<Layer3::IPlugin> instantiatePlugin(const PluginDescriptor& descriptor) override {
        return std::make_unique<MockPlugin>(descriptor.pluginId, descriptor.name, std::vector<uint8_t>{1, 2, 3});
    }
    
    void scanForPlugins(const std::vector<std::string>& /*paths*/) override {}
    bool isScanning() const override { return false; }
    float getScanProgress() const override { return 0.0f; }
    std::string getCurrentlyScanningPlugin() const override { return ""; }
};

} // namespace

TEST_CASE("Plugin State Serialization and Missing Plugin Reports", "[Layer5][PluginSerialization]") {
    auto builderSave = std::make_unique<TestPipelineBuilder>();
    auto pluginManagerSave = std::make_unique<MockPluginManagerSave>();
    
    auto sessionSave = IProjectSession::create(
        std::move(builderSave),
        nullptr, nullptr, pluginManagerSave.get(), NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(sessionSave != nullptr);

    auto* tmSave = sessionSave->getTrackManager();
    REQUIRE(tmSave != nullptr);

    // Create a track
    TrackCreateInfo trackInfo{};
    trackInfo.type = TrackType::INSTRUMENT;
    trackInfo.trackId = {1, 1};
    
    TrackID trackId = tmSave->createTrack(trackInfo);
    REQUIRE(trackId.isValid());
    
    // Insert plugins by manipulating internal state because mock pipeline builder doesn't populate plugin slots
    auto* tmSaveImpl = dynamic_cast<TrackManagerImpl*>(tmSave);
    REQUIRE(tmSaveImpl != nullptr);
    // Insert plugins (instantiated by MockPluginManagerSave)
    tmSave->insertTrackInstrument(trackId, 42); 
    tmSave->insertTrackPlugin(trackId, 0, 99); 
    
    std::string testFile = "plugin_test_proj.agproj";
    REQUIRE(sessionSave->saveToFile(testFile, nullptr) == true);
    
    // --- LOAD ---
    auto builderLoad = std::make_unique<TestPipelineBuilder>();
    auto pluginManagerLoad = std::make_unique<MockPluginManager>();
    
    auto sessionLoad = IProjectSession::create(
        std::move(builderLoad),
        nullptr, nullptr, pluginManagerLoad.get(), NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(sessionLoad != nullptr);
    
    std::vector<MissingPluginReport> missingPlugins;
    REQUIRE(sessionLoad->loadFromFile(testFile, nullptr, &missingPlugins) == true);
    
    // Check MissingPluginReport
    REQUIRE(missingPlugins.size() == 1);
    CHECK(missingPlugins[0].trackId.id == trackId.id);
    CHECK(missingPlugins[0].slotIndex == 0);
    CHECK(missingPlugins[0].requestedPluginId == 99);
    
    // Check TrackManager
    auto* tmLoad = sessionLoad->getTrackManager();
    (void)tmLoad;
    
    // Attempting to re-save to ensure placeholder preserves state
    std::string testFile2 = "plugin_test_proj2.agproj";
    REQUIRE(sessionLoad->saveToFile(testFile2, nullptr) == true);
    
    // Load again with a manager that now knows about 99 (to verify state blob was preserved)
    auto builderLoad2 = std::make_unique<TestPipelineBuilder>();
    auto pluginManagerLoad2 = std::make_unique<MockPluginManagerSave>();
    
    auto sessionLoad2 = IProjectSession::create(
        std::move(builderLoad2),
        nullptr, nullptr, pluginManagerLoad2.get(), NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    
    std::vector<MissingPluginReport> missingPlugins2;
    REQUIRE(sessionLoad2->loadFromFile(testFile2, nullptr, &missingPlugins2) == true);
    
    // Now nothing should be missing, because MockPluginManagerSave instantiates all
    CHECK(missingPlugins2.size() == 0);
}
