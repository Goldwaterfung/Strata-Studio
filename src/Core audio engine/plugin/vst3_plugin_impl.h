// src/Core audio engine/plugin/vst3_plugin_impl.h
#pragma once

#include "iplugin.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/gui/iplugview.h"
#include "../streaming/ibutler_thread.h"
#include <vector>
#include <atomic>

namespace Layer3 {

using InitDllProc = bool (*)();
using ExitDllProc = bool (*)();

class VST3PluginImpl : public IPlugin {
public:
  VST3PluginImpl(const char *path);
  ~VST3PluginImpl() override;

  void setButlerThread(IButlerThread* butler) { m_butler = butler; }

  bool getInfo(PluginInfo &outInfo) const override;

  void processAudio(float *const *inputs, uint32_t numInputs,
                    float *const *outputs, uint32_t numOutputs,
                    uint32_t numSamples, const EventData *events,
                    uint32_t numEvents,
                    EventData *outEvents, uint32_t *outCount,
                    const ::ProcessContext *context,
                    const bool* inputSilence = nullptr) override;

  float getParameterValue(uint32_t paramIndex) const override;
  void setParameterValue(uint32_t paramIndex, float value) override;
  bool getParameterInfo(uint32_t paramIndex, ::ParameterInfo &outInfo) const override;
  void setParameterTweakedCallback(ParameterTweakedCallback cb) override { m_tweakedCallback = std::move(cb); }

  // State management
  std::vector<uint8_t> getState() const override;
  bool loadState(const uint8_t *buffer, uint64_t bufferSize) override;

  bool openEditor(void *parentWindow, int &outWidth, int &outHeight) override;
  void closeEditor() override;

private:
  bool load(const char *path);
  void unload();

  // VST3 pointers
  void *m_module = nullptr;
  void *m_bundleRef = nullptr;
  Steinberg::IPtr<Steinberg::Vst::IComponent> m_component;
  Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> m_processor;
  Steinberg::IPtr<Steinberg::Vst::IEditController> m_controller;
  Steinberg::IPtr<Steinberg::Vst::IMidiMapping> m_midiMapping;
  Steinberg::IPtr<Steinberg::IPlugView> m_view;

  // Processing data
  Steinberg::Vst::ProcessData m_processData;
  Steinberg::Vst::AudioBusBuffers *m_vstInputs = nullptr;
  Steinberg::Vst::AudioBusBuffers *m_vstOutputs = nullptr;

  // VST3 Host Context (Internal)
  class HostContext : public Steinberg::Vst::IComponentHandler,
                      public Steinberg::Vst::IHostApplication,
                      public Steinberg::IPlugFrame {
  public:
    HostContext(VST3PluginImpl *owner) : m_owner(owner) {}
    virtual ~HostContext() = default;

    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void **obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    // IComponentHandler
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized) override;
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;

    // IHostApplication
    Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override;
    Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID _iid, void **obj) override;

    // IPlugFrame
    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override;

  private:
    VST3PluginImpl *m_owner;
  };

  std::unique_ptr<HostContext> m_hostContext;
  void setupProcessContext(const ::ProcessContext *context);
  void setupBuffers(uint32_t numInputs, uint32_t numOutputs);
  Steinberg::Vst::ProcessContext m_vstContext;

  // Event & Parameter Helpers
  Steinberg::IPtr<Steinberg::Vst::IEventList> m_inputEvents;
  Steinberg::IPtr<Steinberg::Vst::IEventList> m_outputEvents;
  Steinberg::IPtr<Steinberg::Vst::IParameterChanges> m_inputParams;
  Steinberg::IPtr<Steinberg::Vst::IParameterChanges> m_outputParams;
  std::vector<Steinberg::Vst::ParamID> m_outputParamMapping; // normalized to standard MIDI output events
  
  ParameterTweakedCallback m_tweakedCallback;

  IButlerThread* m_butler = nullptr;
  std::atomic<bool> m_isStateLoading{false};
  bool m_isInitialized = false;
  double m_currentSampleRate = 0.0;
  uint32_t m_currentMaxBlockSize = 0;
  char m_name[MAX_PLUGIN_NAME_LENGTH] = {0};

  uint32_t m_numInputBuses = 0;
  uint32_t m_numOutputBuses = 0;
  std::vector<uint32_t> m_inputBusChannels;
  std::vector<uint32_t> m_outputBusChannels;
  static constexpr size_t MAX_SUPPORTED_MIDI_BUSES = 8;
  void cacheMidiMappings();
  Steinberg::Vst::ParamID m_midiCCCache[MAX_SUPPORTED_MIDI_BUSES][16][129];
  uint32_t m_numMidiInputBuses = 0;
  std::atomic<bool> m_isProcessing{false};
  static constexpr size_t MAX_SILENT_CHANNELS = 64;
  static constexpr size_t MAX_SILENT_BUFFER_SIZE = 8192;
  alignas(16) float m_silentBuffers[MAX_SILENT_CHANNELS][MAX_SILENT_BUFFER_SIZE] = {{0.0f}};
};

} // namespace Layer3
