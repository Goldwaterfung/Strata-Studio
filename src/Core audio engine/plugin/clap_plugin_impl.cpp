#include "clap_plugin_impl.h"
#include <project_config.h>
#include "Hardware/OS abstraction/audio/audio_utils.h"
#include <dlfcn.h>
#include <iostream>
#include <algorithm>
#include <vector>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

namespace Layer3 {

//==============================================================================
// CLAP Event Support
//==============================================================================

class ClapInputEvents : public clap_input_events_t {
public:
    ClapInputEvents() {
        this->size = size_func;
        this->get = get_func;
    }
    static uint32_t size_func(const clap_input_events_t *list) {
        return (uint32_t)static_cast<const ClapInputEvents*>(list)->m_events.size();
    }
    static const clap_event_header_t* get_func(const clap_input_events_t *list, uint32_t index) {
        auto self = static_cast<const ClapInputEvents*>(list);
        if (index >= self->m_events.size()) return nullptr;
        return self->m_events[index];
    }
    void addEvent(const clap_event_header_t* event) { m_events.push_back(event); }
    void clear() { m_events.clear(); }
private:
    std::vector<const clap_event_header_t*> m_events;
};

class ClapOutputEvents : public clap_output_events_t {
public:
    ClapOutputEvents() {
        this->ctx = this;
        this->try_push = try_push_func;
    }
    static bool try_push_func(const clap_output_events_t *list, const clap_event_header_t *event) {
        auto self = static_cast<ClapOutputEvents*>(list->ctx);
        if (event && event->type == CLAP_EVENT_PARAM_VALUE) {
            auto paramEvent = reinterpret_cast<const clap_event_param_value_t*>(event);
            self->paramEvents.push_back(*paramEvent);
        }
        return true;
    }
    std::vector<clap_event_param_value_t> paramEvents;
};

//==============================================================================
// CLAP Stream Support
//==============================================================================

struct MemoryIStream : public clap_istream_t {
    MemoryIStream(const uint8_t* b, uint64_t s) : buf(b), size(s), pos(0) { this->read = read_func; }
    static int64_t read_func(const clap_istream_t *stream, void *buffer, uint64_t size) {
        auto self = (MemoryIStream*)stream;
        uint64_t toRead = std::min(size, self->size - self->pos);
        if (toRead > 0) { std::memcpy(buffer, self->buf + self->pos, toRead); self->pos += toRead; }
        return (int64_t)toRead;
    }
    const uint8_t* buf; uint64_t size; uint64_t pos;
};

struct MemoryOStream : public clap_ostream_t {
    MemoryOStream(uint8_t* b, uint64_t s) : buf(b), size(s), pos(0) { this->write = write_func; }
    static int64_t write_func(const clap_ostream_t *stream, const void *buffer, uint64_t size) {
        auto self = (MemoryOStream*)stream;
        uint64_t toWrite = std::min(size, self->size - self->pos);
        if (toWrite > 0) { std::memcpy(self->buf + self->pos, buffer, toWrite); self->pos += toWrite; }
        return (int64_t)toWrite;
    }
    uint8_t* buf; uint64_t size; uint64_t pos;
};

//==============================================================================
// Implementation
//==============================================================================

CLAPPluginImpl::CLAPPluginImpl(const char *path) {
  m_eventPool.resize(2048); // Pre-allocate 2048 event slots safely

  m_module = dlopen(path, RTLD_NOW);
  if (!m_module) return;

  auto entry = (const clap_plugin_entry_t *)dlsym(m_module, "clap_plugin_entry");
  if (!entry || !entry->init(path)) return;

  host.clap_version = CLAP_VERSION;
  host.host_data = this;
  host.name = config::PROJECT_DISPLAY_NAME.data();
  host.vendor = config::PROJECT_DISPLAY_NAME.data();
  host.get_extension = get_extension;
  host.request_callback = request_callback;
  host.request_process = request_process;
  host.request_restart = request_restart;

  m_hostParams.rescan = host_params_rescan;
  m_hostParams.clear = host_params_clear;
  m_hostParams.request_flush = host_params_request_flush;

  auto factory = (const clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
  if (factory && factory->get_plugin_count(factory) > 0) {
    plugin = factory->create_plugin(factory, &host, factory->get_plugin_descriptor(factory, 0)->id);
    if (plugin) {
        plugin->init(plugin);
        m_params = (const clap_plugin_params_t*)plugin->get_extension(plugin, CLAP_EXT_PARAMS);
        m_state = (const clap_plugin_state_t*)plugin->get_extension(plugin, CLAP_EXT_STATE);
        m_gui = (const clap_plugin_gui_t*)plugin->get_extension(plugin, CLAP_EXT_GUI);
    }
  }
}

CLAPPluginImpl::~CLAPPluginImpl() {
  if (plugin) {
    if (m_isActive) plugin->deactivate(plugin);
    plugin->destroy(plugin);
    plugin = nullptr;
  }
  if (m_module) {
    dlclose(m_module);
    m_module = nullptr;
  }
}

bool CLAPPluginImpl::getInfo(PluginInfo &outInfo) const {
  if (!plugin) return false;
  auto desc = plugin->desc;
  std::strncpy(outInfo.name, desc->name, sizeof(outInfo.name) - 1);
  outInfo.name[sizeof(outInfo.name) - 1] = '\0';
  outInfo.numInputs = 2; // Simplified
  outInfo.numOutputs = 2;
  outInfo.numParameters = m_params ? m_params->count(plugin) : 0;
  outInfo.hasEditor = (m_gui != nullptr);
  
  outInfo.isInstrument = false;
  if (desc->features) {
      for (int i = 0; desc->features[i]; ++i) {
          if (std::strcmp(desc->features[i], "instrument") == 0 ||
              std::strcmp(desc->features[i], "synthesizer") == 0 ||
              std::strcmp(desc->features[i], "sampler") == 0) {
              outInfo.isInstrument = true;
              break;
          }
      }
  }
  return true;
}

void CLAPPluginImpl::processAudio(float *const *inputs, uint32_t numInputs,
                                  float *const *outputs, uint32_t numOutputs,
                                  uint32_t numSamples, const EventData *events,
                                  uint32_t numEvents,
                                  EventData *outEvents, uint32_t *outCount,
                                  const ProcessContext *context,
                                  const bool* inputSilence) {
  if (!plugin) return;
  (void)inputSilence;

  // Gate real-time processing during asynchronous state/soundbank loading.
  if (m_isStateLoading.load(std::memory_order_acquire)) {
      for (uint32_t ch = 0; ch < numOutputs; ++ch) {
          if (outputs[ch]) {
              std::memset(outputs[ch], 0, numSamples * sizeof(float));
          }
      }
      if (outCount) *outCount = 0;
      return;
  }

  if (outCount) *outCount = 0;
  (void)outEvents;

  // 1. Activation & Runtime Check
  if (!m_isActive || m_currentSampleRate != context->sampleRate) {
      if (m_isActive) plugin->deactivate(plugin);
      plugin->activate(plugin, (double)context->sampleRate, (uint32_t)context->maxBlockSize, (uint32_t)numSamples);
      m_isActive = true;
      m_currentSampleRate = context->sampleRate;
  }

  clap_process_t process;
  process.steady_time = (int64_t)context->transport.positionSample;
  process.frames_count = numSamples;
  
  // 2. Map Transport
  ClapEventStorage transportStorage;
  transportStorage.header.size = sizeof(clap_event_transport_t);
  transportStorage.header.time = 0;
  transportStorage.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  transportStorage.header.type = CLAP_EVENT_TRANSPORT;
  transportStorage.header.flags = 0;
  auto &tr = transportStorage.transport;
  tr.header = transportStorage.header;
  tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
  tr.tempo = (double)context->transport.bpm;
  tr.song_pos_beats = (int64_t)((double)context->transport.positionSample * (double)context->transport.bpm / (60.0 * (double)context->sampleRate)); // Simplified

  // 3. Map Audio Buffers (Zero-Copy)
  clap_audio_buffer_t in_buf, out_buf;
  if (numInputs > 0) {
      in_buf.data32 = const_cast<float**>(inputs);
      in_buf.channel_count = numInputs;
      in_buf.latency = 0;
      in_buf.constant_mask = 0;
      process.audio_inputs = &in_buf;
      process.audio_inputs_count = 1;
  } else {
      process.audio_inputs = nullptr;
      process.audio_inputs_count = 0;
  }

  if (numOutputs > 0) {
      out_buf.data32 = const_cast<float**>(outputs);
      out_buf.channel_count = numOutputs;
      out_buf.latency = 0;
      out_buf.constant_mask = 0;
      process.audio_outputs = &out_buf;
      process.audio_outputs_count = 1;
  } else {
      process.audio_outputs = nullptr;
      process.audio_outputs_count = 0;
  }

  // 4. Map Events
  ClapInputEvents inEvents;
  inEvents.addEvent(&transportStorage.header);
  
  uint32_t safeEventsCount = std::min(numEvents, 2048u);
  for (uint32_t i = 0; i < safeEventsCount; ++i) {
      const auto& ev = events[i];
      auto &storage = m_eventPool[i];
      storage.header.time = ev.sampleOffset;
      storage.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      storage.header.flags = 0;
      
      if (ev.eventType == EventType::MIDI_NOTE_ON) {
          storage.header.type = CLAP_EVENT_NOTE_ON;
          storage.header.size = sizeof(clap_event_note_t);
          storage.note.header = storage.header;
          storage.note.port_index = 0;
          storage.note.channel = (int16_t)ev.payload.midiNote.channel;
          storage.note.key = (int16_t)ev.payload.midiNote.pitch;
          storage.note.velocity = (double)ev.payload.midiNote.velocity / 127.0;
          inEvents.addEvent(&storage.header);
      } else if (ev.eventType == EventType::MIDI_NOTE_OFF) {
          storage.header.type = CLAP_EVENT_NOTE_OFF;
          storage.header.size = sizeof(clap_event_note_t);
          storage.note.header = storage.header;
          storage.note.port_index = 0;
          storage.note.channel = (int16_t)ev.payload.midiNote.channel;
          storage.note.key = (int16_t)ev.payload.midiNote.pitch;
          storage.note.velocity = (double)ev.payload.midiNote.velocity / 127.0;
          inEvents.addEvent(&storage.header);
      } else if (ev.eventType == EventType::AUTOMATION) {
          storage.header.type = CLAP_EVENT_PARAM_VALUE;
          storage.header.size = sizeof(clap_event_param_value_t);
          storage.param.header = storage.header;
          storage.param.port_index = -1;
          storage.param.key = -1;
          storage.param.channel = -1;
          storage.param.param_id = static_cast<clap_id>(ev.payload.automation.parameterIndex);
          storage.param.value = static_cast<double>(ev.payload.automation.targetValue);
          storage.param.note_id = -1;
          storage.param.cookie = nullptr;
          inEvents.addEvent(&storage.header);
      }
  }

  process.in_events = &inEvents;
  process.out_events = nullptr;

  {
      Layer1::ScopedDenormalHandler denormalHandler;
      plugin->process(plugin, &process);
  }
}

const void *CLAPPluginImpl::get_extension(const struct clap_host *host, const char *extension_id) {
  if (!host || !extension_id) return nullptr;
  auto self = static_cast<CLAPPluginImpl*>(host->host_data);
  if (self && std::strcmp(extension_id, CLAP_EXT_PARAMS) == 0) {
      return &self->m_hostParams;
  }
  return nullptr; 
}

void CLAPPluginImpl::request_callback(const struct clap_host *host) { (void)host; }
void CLAPPluginImpl::request_process(const struct clap_host *host) { (void)host; }
void CLAPPluginImpl::request_restart(const struct clap_host *host) { (void)host; }

void CLAPPluginImpl::host_params_rescan(const clap_host_t *host, clap_param_rescan_flags flags) {
    (void)host; (void)flags;
}

void CLAPPluginImpl::host_params_clear(const clap_host_t *host, clap_id param_id, clap_param_clear_flags flags) {
    (void)host; (void)param_id; (void)flags;
}

void CLAPPluginImpl::host_params_request_flush(const clap_host_t *host) {
    if (!host || !host->host_data) return;
    auto self = static_cast<CLAPPluginImpl*>(host->host_data);
#ifdef __APPLE__
    dispatch_async_f(dispatch_get_main_queue(), self, [](void* ctx) {
        auto pluginImpl = static_cast<CLAPPluginImpl*>(ctx);
        if (!pluginImpl || !pluginImpl->plugin || !pluginImpl->m_params) return;
        
        ClapInputEvents inEvents;
        ClapOutputEvents outEvents;
        pluginImpl->m_params->flush(pluginImpl->plugin, &inEvents, &outEvents);
        
        if (pluginImpl->m_tweakedCallback) {
            for (const auto& ev : outEvents.paramEvents) {
                ::ParameterInfo info{};
                if (pluginImpl->getParameterInfo(static_cast<uint32_t>(ev.param_id), info)) {
                    float range = info.maxValue - info.minValue;
                    float normalized = range > 0.0f ? (static_cast<float>(ev.value) - info.minValue) / range : 0.0f;
                    normalized = std::clamp(normalized, 0.0f, 1.0f);
                    pluginImpl->m_tweakedCallback(static_cast<uint32_t>(ev.param_id), normalized);
                }
            }
        }
    });
#endif
}

float CLAPPluginImpl::getParameterValue(uint32_t paramIndex) const {
  if (!m_params || !plugin) return 0.0f;
  double value = 0.0;
  m_params->get_value(plugin, (clap_id)paramIndex, &value);
  return (float)value;
}

bool CLAPPluginImpl::getParameterInfo(uint32_t paramIndex, ::ParameterInfo &outInfo) const {
  if (!m_params || !plugin) return false;
  
  clap_param_info_t clapInfo{};
  if (!m_params->get_info(plugin, paramIndex, &clapInfo)) {
    return false;
  }
  
  outInfo.index = clapInfo.id;
  std::strncpy(outInfo.name, clapInfo.name, sizeof(outInfo.name) - 1);
  outInfo.name[sizeof(outInfo.name) - 1] = '\0';
  outInfo.unit[0] = '\0';
  
  outInfo.minValue = static_cast<float>(clapInfo.min_value);
  outInfo.maxValue = static_cast<float>(clapInfo.max_value);
  outInfo.defaultValue = static_cast<float>(clapInfo.default_value);
  
  outInfo.flags = ::ParameterInfo::NONE;
  if (clapInfo.flags & CLAP_PARAM_IS_AUTOMATABLE) {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_AUTOMATABLE);
  }
  if (clapInfo.flags & CLAP_PARAM_IS_READONLY) {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_READ_ONLY);
  }
  if (clapInfo.flags & CLAP_PARAM_IS_HIDDEN) {
    // Hidden parameters must not appear in the automation lane picker UI.
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_HIDDEN);
  }
  if (clapInfo.flags & CLAP_PARAM_IS_BYPASS) {
    // Dedicated bypass toggle: treat as a boolean parameter for step-curve and snapping.
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_BOOLEAN);
  }
  if (clapInfo.flags & CLAP_PARAM_IS_MODULATABLE) {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_MODULATABLE);
  }
  if (clapInfo.flags & CLAP_PARAM_IS_STEPPED) {
    if (clapInfo.min_value == 0.0 && clapInfo.max_value == 1.0) {
      outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_BOOLEAN);
    } else {
      outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_INTEGER);
    }
  }
  
  return true;
}

void CLAPPluginImpl::setParameterValue(uint32_t paramIndex, float value) {
  // CLAP parameters are typically set via events during process call for sample accuracy.
  // For immediate host-side set, we can use the extension if allowed.
  (void)paramIndex; (void)value;
}

std::vector<uint8_t> CLAPPluginImpl::getState() const {
  if (!plugin || !m_state) return {};
  
  std::vector<uint8_t> buf;
  
  clap_ostream_t stream;
  stream.ctx = &buf;
  stream.write = [](const clap_ostream *stream, const void *buffer, uint64_t size) -> int64_t {
      auto* b = static_cast<std::vector<uint8_t>*>(stream->ctx);
      uint32_t currentSize = b->size();
      b->resize(currentSize + size);
      std::memcpy(b->data() + currentSize, buffer, size);
      return size;
  };
  
  if (m_state->save(plugin, &stream)) {
      return buf;
  }
  
  return {};
}

bool CLAPPluginImpl::loadState(const uint8_t *buffer, uint64_t bufferSize) {
  if (!m_state || !plugin) return false;

  auto stateData = std::make_shared<std::vector<uint8_t>>(buffer, buffer + bufferSize);
  auto loadTask = [this, stateData]() {
      MemoryIStream stream(stateData->data(), stateData->size());
      m_state->load(plugin, &stream);
      m_isStateLoading = false;
  };
  if (m_butler) { m_isStateLoading = true; m_butler->scheduleTask(loadTask); return true; }
  else { loadTask(); return true; }
}

bool CLAPPluginImpl::openEditor(void *parentWindow, int &outWidth, int &outHeight) {
  if (!m_gui || !plugin) return false;
  m_gui->create(plugin, CLAP_WINDOW_API_COCOA, false);

  uint32_t w = 0, h = 0;
  if (m_gui->get_size(plugin, &w, &h)) {
    outWidth = static_cast<int>(w);
    outHeight = static_cast<int>(h);
  } else {
    outWidth = 640;
    outHeight = 480;
  }

  m_gui->set_parent(plugin, (clap_window_t*)parentWindow);
  m_gui->show(plugin);
  return true;
}

void CLAPPluginImpl::closeEditor() {
    if (m_gui && plugin) m_gui->destroy(plugin);
}

} // namespace Layer3
