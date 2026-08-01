#pragma once

#include "iplugin.h"
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

namespace Layer3 {

class PlaceholderPlugin : public IPlugin {
public:
    PlaceholderPlugin(uint32_t originalPluginId, const std::string& originalName, const std::vector<uint8_t>& stateBlob)
        : originalName_(originalName), stateBlob_(stateBlob) {
        (void)originalPluginId;
    }

    ~PlaceholderPlugin() override = default;

    bool getInfo(PluginInfo &outInfo) const override {
        outInfo.numInputs = 2;
        outInfo.numOutputs = 2;
        outInfo.numParameters = 0;
        outInfo.latencySamples = 0;
        outInfo.hasEditor = false;
        outInfo.isInstrument = false;
        std::strncpy(outInfo.name, originalName_.c_str(), MAX_PLUGIN_NAME_LENGTH - 1);
        outInfo.name[MAX_PLUGIN_NAME_LENGTH - 1] = '\0';
        return true;
    }

    void processAudio(float *const *inputs, uint32_t numInputs,
                      float *const *outputs, uint32_t numOutputs,
                      uint32_t numSamples, const EventData* /*events*/,
                      uint32_t /*numEvents*/,
                      EventData* /*outEvents*/, uint32_t* outCount,
                      const ProcessContext* /*context*/,
                      const bool* /*inputSilence*/ = nullptr) override {
        
        if (outCount) *outCount = 0;

        // Transparent bypass: copy inputs to outputs
        uint32_t channelsToCopy = std::min(numInputs, numOutputs);
        for (uint32_t c = 0; c < channelsToCopy; ++c) {
            if (inputs[c] && outputs[c]) {
                std::memcpy(outputs[c], inputs[c], numSamples * sizeof(float));
            }
        }
        // Silence remaining outputs
        for (uint32_t c = channelsToCopy; c < numOutputs; ++c) {
            if (outputs[c]) {
                std::memset(outputs[c], 0, numSamples * sizeof(float));
            }
        }
    }

    float getParameterValue(uint32_t /*paramIndex*/) const override { return 0.0f; }
    void setParameterValue(uint32_t /*paramIndex*/, float /*value*/) override {}
    bool getParameterInfo(uint32_t /*paramIndex*/, ::ParameterInfo& /*outInfo*/) const override { return false; }
    void setParameterTweakedCallback(ParameterTweakedCallback /*cb*/) override {}

    std::vector<uint8_t> getState() const override {
        return stateBlob_;
    }

    bool loadState(const uint8_t *buffer, uint64_t bufferSize) override {
        stateBlob_.assign(buffer, buffer + bufferSize);
        return true;
    }

    bool openEditor(void* /*parentWindow*/, int& /*outWidth*/, int& /*outHeight*/) override { return false; }
    void closeEditor() override {}

private:
    std::string originalName_;
    std::vector<uint8_t> stateBlob_;
};

} // namespace Layer3
