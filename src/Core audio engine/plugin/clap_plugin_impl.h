// src/Core audio engine/plugin/clap_plugin_impl.h
#pragma once

#include "iplugin.h"
#include <clap/clap.h>
#include "../streaming/ibutler_thread.h"
#include <atomic>
#include <vector>

namespace Layer3 {

struct ClapEventStorage {
    union {
        clap_event_note_t note;
        clap_event_param_value_t param;
        clap_event_transport_t transport;
        clap_event_midi_t midi;
    };
    clap_event_header_t header;
};

class CLAPPluginImpl : public IPlugin {
public:
  CLAPPluginImpl(const char *path);
  ~CLAPPluginImpl() override;

  void setButlerThread(IButlerThread* butler) { m_butler = butler; }

  bool getInfo(PluginInfo &outInfo) const override;

  void processAudio(float *const *inputs, uint32_t numInputs,
                    float *const *outputs, uint32_t numOutputs,
                    uint32_t numSamples, const EventData *events,
                    uint32_t numEvents, EventData *outEvents,
                    uint32_t *outCount, const ProcessContext *context,
                    const bool* inputSilence = nullptr) override;

  float getParameterValue(uint32_t paramIndex) const override;
  void setParameterValue(uint32_t paramIndex, float value) override;
  bool getParameterInfo(uint32_t paramIndex, ::ParameterInfo &outInfo) const override;
  void setParameterTweakedCallback(ParameterTweakedCallback cb) override { m_tweakedCallback = std::move(cb); }

  std::vector<uint8_t> getState() const override;
  bool loadState(const uint8_t *buffer, uint64_t bufferSize) override;

  bool openEditor(void *parentWindow, int &outWidth, int &outHeight) override;
  void closeEditor() override;

private:
  // CLAP pointers
  const clap_plugin_t *plugin = nullptr;
  clap_host_t host;
  void *m_module = nullptr;

  // Extensions
  const clap_plugin_params_t *m_params = nullptr;
  const clap_plugin_state_t *m_state = nullptr;
  const clap_plugin_gui_t *m_gui = nullptr;

  // Host Extension Callbacks
  static const void *get_extension(const struct clap_host *host, const char *extension_id);
  static void request_callback(const struct clap_host *host);
  static void request_process(const struct clap_host *host);
  static void request_restart(const struct clap_host *host);

  clap_host_params_t m_hostParams;
  static void host_params_rescan(const clap_host_t *host, clap_param_rescan_flags flags);
  static void host_params_clear(const clap_host_t *host, clap_id param_id, clap_param_clear_flags flags);
  static void host_params_request_flush(const clap_host_t *host);

  IButlerThread* m_butler = nullptr;
  std::atomic<bool> m_isStateLoading{false};
  bool m_isActive = false;
  float m_currentSampleRate = 0.0f;
  uint32_t m_currentMaxBlockSize = 0;
  std::vector<ClapEventStorage> m_eventPool;
  ParameterTweakedCallback m_tweakedCallback;
};

} // namespace Layer3
