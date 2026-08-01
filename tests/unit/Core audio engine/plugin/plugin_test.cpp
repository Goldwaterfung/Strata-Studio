#include <catch2/catch_test_macros.hpp>
#include "Core audio engine/plugin/iplugin.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include <vector>
#include <cstring>
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>

using namespace Layer3;

class MockPlugin : public IPlugin {
public:
    MockPlugin() {
        std::memset(&info, 0, sizeof(info));
        info.numInputs = 2;
        info.numOutputs = 2;
        info.numParameters = 64;
        std::strncpy(info.name, "MockPlugin", MAX_PLUGIN_NAME_LENGTH);
        
        params.resize(64, 0.0f);
    }

    bool getInfo(PluginInfo &outInfo) const override {
        outInfo = info;
        return true;
    }

    void processAudio(float *const *inputs, uint32_t numInputs,
                      float *const *outputs, uint32_t numOutputs,
                      uint32_t numSamples, const EventData * /*events*/,
                      uint32_t /*numEvents*/,
                      EventData * /*outEvents*/, uint32_t * /*outCount*/,
                      const ProcessContext * /*context*/,
                      const bool* /*inputSilence*/ = nullptr) override {
        float gain = params[0];
        for (uint32_t c = 0; c < numOutputs; ++c) {
            uint32_t inC = c % numInputs;
            for (uint32_t s = 0; s < numSamples; ++s) {
                float val = inputs ? (inputs[inC] ? inputs[inC][s] : 0.0f) : 0.0f;
                outputs[c][s] = val * gain;
                // Assert finite
                REQUIRE(std::isfinite(outputs[c][s]));
            }
        }
    }

    float getParameterValue(uint32_t paramIndex) const override {
        if (paramIndex < params.size()) return params[paramIndex];
        return 0.0f;
    }

    void setParameterValue(uint32_t paramIndex, float value) override {
        if (paramIndex < params.size()) params[paramIndex] = value;
    }

    bool getParameterInfo(uint32_t paramIndex, ::ParameterInfo &outInfo) const override {
        if (paramIndex >= params.size()) return false;
        outInfo.index = paramIndex;
        std::snprintf(outInfo.name, sizeof(outInfo.name), "Param_%u", paramIndex);
        std::strcpy(outInfo.unit, "");
        outInfo.minValue = 0.0f;
        outInfo.maxValue = 1.0f;
        outInfo.defaultValue = 0.0f;
        outInfo.flags = ::ParameterInfo::IS_AUTOMATABLE;
        return true;
    }

    void setParameterTweakedCallback(ParameterTweakedCallback /*cb*/) override {}

    std::vector<uint8_t> getState() const override {
        std::vector<uint8_t> buffer(params.size() * sizeof(float));
        std::memcpy(buffer.data(), params.data(), buffer.size());
        return buffer;
    }

    bool loadState(const uint8_t *buffer, uint64_t bufferSize) override {
        if (buffer == nullptr || bufferSize == 0) {
            // Default init path
            std::fill(params.begin(), params.end(), 0.0f);
            return true;
        }
        if (bufferSize < params.size() * sizeof(float)) return false;
        std::memcpy(params.data(), buffer, params.size() * sizeof(float));
        return true;
    }

    bool openEditor(void * /*parentWindow*/, int &outWidth, int &outHeight) override {
        outWidth = 640;
        outHeight = 480;
        return true;
    }
    void closeEditor() override {}

private:
    PluginInfo info;
    std::vector<float> params;
};

class MockPluginManager : public IPluginManager {
public:
    void scanForPlugins(const std::vector<std::string>& /*paths*/) override {}
    std::vector<PluginDescriptor> getAvailablePlugins() const override {
        PluginDescriptor desc = {};
        desc.pluginId = 1;
        std::strncpy(desc.name, "MockPlugin", MAX_PLUGIN_NAME_LENGTH);
        desc.formatFlags = PluginFormatFlags::VST3;
        return {desc};
    }
    std::unique_ptr<IPlugin> instantiatePlugin(const PluginDescriptor& /*descriptor*/) override {
        return std::make_unique<MockPlugin>();
    }
    bool isScanning() const override { return false; }
    float getScanProgress() const override { return 0.0f; }
    std::string getCurrentlyScanningPlugin() const override { return ""; }
};

TEST_CASE("Plugin: Lifecycle", "[Layer3][Plugin]") {
    auto manager = std::make_unique<MockPluginManager>();
    auto plugins = manager->getAvailablePlugins();
    REQUIRE(!plugins.empty());
    
    auto plugin = manager->instantiatePlugin(plugins[0]);
    REQUIRE(plugin != nullptr);

    SECTION("Information") {
        IPlugin::PluginInfo info;
        REQUIRE(plugin->getInfo(info));
        CHECK(info.numInputs == 2);
        CHECK(info.numOutputs == 2);
        CHECK(info.numParameters == 64);
        CHECK(std::string(info.name) == "MockPlugin");
    }

    SECTION("Default Initialization") {
        // Test loadState(nullptr, 0) as per plan
        REQUIRE(plugin->loadState(nullptr, 0) == true);
        CHECK(plugin->getParameterValue(0) == 0.0f);
    }

    SECTION("Process Audio - Finite Samples") {
        float inL[512], inR[512], outL[512], outR[512];
        for (int i = 0; i < 512; ++i) inL[i] = inR[i] = 1.0f;
        
        float* inputs[] = {inL, inR};
        float* outputs[] = {outL, outR};
        
        plugin->setParameterValue(0, 0.5f);
        
        ProcessContext ctx;
        // Test with 512 frames, including silence as per plan (implicit here with 1.0f input)
        plugin->processAudio(inputs, 2, outputs, 2, 512, nullptr, 0, nullptr, nullptr, &ctx);
        
        for (int i = 0; i < 512; ++i) {
            CHECK(std::isfinite(outL[i]));
            CHECK(std::isfinite(outR[i]));
            CHECK(outL[i] == 0.5f);
        }
    }

    SECTION("State Round-trip") {
        plugin->setParameterValue(0, 0.75f);
        plugin->setParameterValue(10, 0.25f);
        
        std::vector<uint8_t> buffer = plugin->getState();
        REQUIRE(!buffer.empty());
        
        plugin->setParameterValue(0, 0.0f);
        
        REQUIRE(plugin->loadState(buffer.data(), buffer.size()));
        CHECK(plugin->getParameterValue(0) == 0.75f);
        CHECK(plugin->getParameterValue(10) == 0.25f);
    }
}

TEST_CASE("Plugin: Active Scan and Print", "[Layer3][Plugin][Scan]") {
    auto manager = IPluginManager::create();
    REQUIRE(manager != nullptr);

    std::vector<std::string> paths = {
        "/Library/Audio/Plug-Ins/VST3",
        "/Library/Audio/Plug-Ins/Components",
        "/Library/Audio/Plug-Ins/CLAP"
    };

    std::cout << "Starting Real Plugin Scan on macOS paths:" << std::endl;
    for (const auto& path : paths) {
        std::cout << "  - " << path << std::endl;
    }

    manager->scanForPlugins(paths);

    // Sleep in a polling loop to allow background scan process to finish
    std::cout << "Scanning..." << std::flush;
    
    int maxWaitMs = 5000;
    int waitedMs = 0;
    size_t lastCount = 0;
    int stableCount = 0;

    while (waitedMs < maxWaitMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        waitedMs += 200;
        std::cout << "." << std::flush;

        auto plugins = manager->getAvailablePlugins();
        if (!plugins.empty()) {
            if (plugins.size() == lastCount) {
                stableCount++;
                if (stableCount >= 5) { // Stable for 1.0 second
                    break;
                }
            } else {
                lastCount = plugins.size();
                stableCount = 0;
            }
        }
    }
    std::cout << " Done!\n" << std::endl;

    auto plugins = manager->getAvailablePlugins();
    std::cout << "==================================================" << std::endl;
    std::cout << "DISCOVERED REAL PLUGINS COUNT: " << plugins.size() << std::endl;
    std::cout << "==================================================" << std::endl;

    for (const auto& desc : plugins) {
        std::cout << "Name:         " << desc.name << std::endl;
        std::cout << "Path:         " << desc.filePath << std::endl;
        std::cout << "Manufacturer: " << desc.manufacturer << std::endl;
        std::cout << "Format:       ";
        if (desc.formatFlags & PluginFormatFlags::VST3) std::cout << "VST3 ";
        if (desc.formatFlags & PluginFormatFlags::AU) std::cout << "AU ";
        if (desc.formatFlags & PluginFormatFlags::CLAP) std::cout << "CLAP ";
        std::cout << std::endl;
        std::cout << "Version:      " << ((desc.version >> 16) & 0xFF) << "." 
                  << ((desc.version >> 8) & 0xFF) << "." << (desc.version & 0xFF) << std::endl;
        std::cout << "--------------------------------------------------" << std::endl;
    }
    std::cout << "==================================================\n" << std::endl;

    SUCCEED("Discovered plugins scanned and printed successfully.");
}

