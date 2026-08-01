// src/Core audio engine/plugin/au_plugin_impl.h
#pragma once

#ifdef __APPLE__
// To avoid clash with CoreAudio's AudioBuffer defined in global namespace,
// we rename the system version while including Apple headers.
#define AudioBuffer CA_AudioBuffer
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#undef AudioBuffer
#endif // __APPLE__

#include "iplugin.h"
#include "../streaming/ibutler_thread.h"
#include <atomic>
#include <vector>

namespace Layer3 {

class AUPluginImpl : public IPlugin {
public:
  AUPluginImpl(const char *path);
  ~AUPluginImpl() override;

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
#ifdef __APPLE__
  AudioUnit auInstance = nullptr;
  static OSStatus beatAndTempoProc(void *inHostUserData, Float64 *outCurrentBeat, Float64 *outCurrentTempo);
  static OSStatus musicalTimeLocationProc(void *inHostUserData, UInt32 *outDeltaSampleOffsetToNextBeat, Float32 *outTimeSigNumerator, UInt32 *outTimeSigDenominator, Float64 *outCurrentMeasureDownbeat);
  static OSStatus transportStateProc(void *inHostUserData, Boolean *outIsPlaying, Boolean *outTransportStateChanged, Float64 *outSampleTickPosition, Boolean *outIsCycling, Float64 *outCycleStartBeat, Float64 *outCycleEndBeat);
  static OSStatus inputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const ::AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, ::AudioBufferList *ioData);
#endif // __APPLE__

  IButlerThread* m_butler = nullptr;
  std::atomic<bool> m_isStateLoading{false};
#ifdef __APPLE__
  const ProcessContext* m_currentContext = nullptr;
  float* const* m_currentInputs = nullptr;
  uint32_t m_currentNumInputs = 0;
  uint64_t m_continuousSampleTime = 0; // Added for continuous clock
  bool m_isInstrument = false;
  EventData* m_currentOutEvents = nullptr;
  uint32_t* m_currentOutCount = nullptr;
  static OSStatus midiOutputCallback(void *userData, const ::AudioTimeStamp *timeStamp, UInt32 midiOutNum, const ::MIDIPacketList *pktlist);
  void queueMIDIOutputForNextBuffer(const ::MIDIPacketList *pktlist);

  struct ParameterRange {
      float minValue;
      float maxValue;
      float defaultValue;
      float range() const {
          float r = maxValue - minValue;
          return (r == 0.0f) ? 1.0f : r;
      }
  };
  AUParameterListenerRef m_auParameterListener = nullptr;
  std::vector<AudioUnitParameterID> m_parameterIDs;
  std::vector<ParameterRange> m_parameterRanges;
  std::vector<void*> m_openNSViews;
#endif // __APPLE__
  ParameterTweakedCallback m_tweakedCallback;
};

} // namespace Layer3
