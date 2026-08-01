// src/Core audio engine/plugin/iplugin.h
#pragma once

#include "system_primitives.h"
#include <cstdint>
#include <memory>
#include <functional>

namespace Layer3 {

//==============================================================================
// UNIFIED PLUGIN INTERFACE
//==============================================================================

class IPlugin {
public:
  virtual ~IPlugin() = default;

  //==========================================================================
  // Information
  //==========================================================================
  struct PluginInfo {
    uint32_t numInputs;
    uint32_t numOutputs;
    uint32_t numParameters;
    uint32_t latencySamples;
    bool hasEditor;
    bool isInstrument;
    char name[MAX_PLUGIN_NAME_LENGTH];
  };

  virtual bool getInfo(PluginInfo &outInfo) const = 0;

  //==========================================================================
  // Audio Processing (RT-Safe, Wait-Free)
  //==========================================================================

  // Process audio and events
  // Translates internal planar buffers and events into format-specific
  // structures (e.g., VST3 ProcessData, AU AudioBufferList)
  virtual void processAudio(float *const *inputs, uint32_t numInputs,
                            float *const *outputs, uint32_t numOutputs,
                            uint32_t numSamples, const EventData *events,
                            uint32_t numEvents,
                            EventData *outEvents, uint32_t *outCount,
                            const ProcessContext *context,
                            const bool* inputSilence = nullptr) = 0;

  //==========================================================================
  // Parameter & State Management
  //==========================================================================

  virtual float getParameterValue(uint32_t paramIndex) const = 0;
  virtual void setParameterValue(uint32_t paramIndex, float value) = 0;
  virtual bool getParameterInfo(uint32_t paramIndex, ::ParameterInfo &outInfo) const = 0;

  using ParameterTweakedCallback = std::function<void(uint32_t paramIndex, float value)>;
  virtual void setParameterTweakedCallback(ParameterTweakedCallback cb) = 0;

  // State management (e.g., chunks for VST)
  virtual std::vector<uint8_t> getState() const = 0;
  virtual bool loadState(const uint8_t *buffer, uint64_t bufferSize) = 0;

  //==========================================================================
  // GUI / Editor
  //==========================================================================

  // Attach plugin editor to a native window handle
  // Parameters:
  //   parentWindow: HWND on Windows, NSView* on macOS
  virtual bool openEditor(void *parentWindow, int &outWidth, int &outHeight) = 0;
  virtual void closeEditor() = 0;
};

} // namespace Layer3
