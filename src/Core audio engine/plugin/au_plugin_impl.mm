// src/Core audio engine/plugin/au_plugin_impl.mm
#include "au_plugin_impl.h"
#include "Hardware/OS abstraction/audio/audio_utils.h"

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <AudioUnit/AUCocoaUIView.h>
#endif

namespace Layer3 {

AUPluginImpl::AUPluginImpl(const char *path) {
#ifdef __APPLE__
  AudioComponentDescription desc;
  std::memset(&desc, 0, sizeof(desc));
  
  auto stringToOSType = [](const char* s) {
    return (OSType)((s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3]);
  };
  char t[5] = {0}, st[5] = {0}, m[5] = {0};
  bool parsed = false;
  if (path) {
      // 1. Try format with path prefix first (path:type:subtype:manuf)
      if (sscanf(path, "%*[^:]:%4[^:]:%4[^:]:%4s", t, st, m) == 3) {
          parsed = true;
      }
      // 2. Try raw format without path prefix (type:subtype:manuf)
      else if (sscanf(path, "%4[^:]:%4[^:]:%4s", t, st, m) == 3) {
          parsed = true;
      }
  }

  if (!parsed && path) {
      // 3. Fallback: Parse AudioComponents type, subtype, manufacturer directly from bundle Info.plist
      std::string plistPath = std::string(path) + "/Contents/Info.plist";
      CFStringRef pathCF = CFStringCreateWithCString(nullptr, plistPath.c_str(), kCFStringEncodingUTF8);
      if (pathCF) {
          CFURLRef plistURL = CFURLCreateWithFileSystemPath(nullptr, pathCF, kCFURLPOSIXPathStyle, false);
          CFRelease(pathCF);
          if (plistURL) {
              CFReadStreamRef stream = CFReadStreamCreateWithFile(kCFAllocatorDefault, plistURL);
              if (stream) {
                  if (CFReadStreamOpen(stream)) {
                      CFPropertyListRef plist = CFPropertyListCreateWithStream(kCFAllocatorDefault, stream, 0, kCFPropertyListImmutable, nullptr, nullptr);
                      if (plist && CFDictionaryGetTypeID() == CFGetTypeID(plist)) {
                          CFDictionaryRef dict = static_cast<CFDictionaryRef>(plist);
                          CFArrayRef components = static_cast<CFArrayRef>(CFDictionaryGetValue(dict, CFSTR("AudioComponents")));
                          if (components && CFArrayGetTypeID() == CFGetTypeID(components) && CFArrayGetCount(components) > 0) {
                              CFDictionaryRef compDict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(components, 0));
                              if (compDict && CFDictionaryGetTypeID() == CFGetTypeID(compDict)) {
                                  CFStringRef typeStr = static_cast<CFStringRef>(CFDictionaryGetValue(compDict, CFSTR("type")));
                                  CFStringRef subtypeStr = static_cast<CFStringRef>(CFDictionaryGetValue(compDict, CFSTR("subtype")));
                                  CFStringRef manufacturerStr = static_cast<CFStringRef>(CFDictionaryGetValue(compDict, CFSTR("manufacturer")));
                                  
                                  if (typeStr && subtypeStr && manufacturerStr) {
                                      CFStringGetCString(typeStr, t, sizeof(t), kCFStringEncodingUTF8);
                                      CFStringGetCString(subtypeStr, st, sizeof(st), kCFStringEncodingUTF8);
                                      CFStringGetCString(manufacturerStr, m, sizeof(m), kCFStringEncodingUTF8);
                                      if (std::strlen(t) == 4 && std::strlen(st) == 4 && std::strlen(m) == 4) {
                                          parsed = true;
                                      }
                                  }
                              }
                          }
                          CFRelease(plist);
                      }
                      CFReadStreamClose(stream);
                  }
                  CFRelease(stream);
              }
              CFRelease(plistURL);
          }
      }
  }

  if (parsed) {
    desc.componentType = stringToOSType(t);
    desc.componentSubType = stringToOSType(st);
    desc.componentManufacturer = stringToOSType(m);
  } else {
    return; // Prevent loading wildcard component when AudioComponent identification fails
  }

  m_isInstrument = (desc.componentType == kAudioUnitType_MusicDevice || desc.componentType == kAudioUnitType_MusicEffect);

  AudioComponent component = AudioComponentFindNext(nullptr, &desc);
  if (component && AudioComponentInstanceNew(component, &auInstance) == noErr) {
    // 1. Set Stream Format (Assume 2-channel for now, will be updated by host if needed)
    AudioStreamBasicDescription asbd;
    std::memset(&asbd, 0, sizeof(asbd));
    asbd.mSampleRate = 44100.0; // Default
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    asbd.mBitsPerChannel = 32;
    asbd.mChannelsPerFrame = 2;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = 4;
    asbd.mBytesPerPacket = 4;

    AudioUnitSetProperty(auInstance, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, sizeof(asbd));
    AudioUnitSetProperty(auInstance, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &asbd, sizeof(asbd));

    // 2. Set Max Frames
    UInt32 maxFrames = 4096;
    AudioUnitSetProperty(auInstance, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof(maxFrames));

    // 3. Set Host Callbacks
    HostCallbackInfo callbacks;
    std::memset(&callbacks, 0, sizeof(callbacks));
    callbacks.hostUserData = this;
    callbacks.beatAndTempoProc = beatAndTempoProc;
    callbacks.musicalTimeLocationProc = musicalTimeLocationProc;
    callbacks.transportStateProc = transportStateProc;
    AudioUnitSetProperty(auInstance, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &callbacks, sizeof(callbacks));

    // 4. Set Input Callback (for Effects)
    AURenderCallbackStruct inputCB;
    inputCB.inputProc = inputCallback;
    inputCB.inputProcRefCon = this;
    AudioUnitSetProperty(auInstance, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &inputCB, sizeof(inputCB));

    // 5. MIDI Output Callback (for Virtual Keyboards/Arpeggiators)
    if (m_isInstrument) {
      AUMIDIOutputCallbackStruct midiCallbackInfo;
      midiCallbackInfo.userData = this;
      midiCallbackInfo.midiOutputCallback = midiOutputCallback;
      AudioUnitSetProperty(auInstance, kAudioUnitProperty_MIDIOutputCallback, kAudioUnitScope_Global, 0, &midiCallbackInfo, sizeof(midiCallbackInfo));
    }

    AudioUnitInitialize(auInstance);

    UInt32 dataSize = 0;
    Boolean writable = false;
    if (AudioUnitGetPropertyInfo(auInstance, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &dataSize, &writable) == noErr && dataSize > 0) {
        uint32_t numParams = dataSize / sizeof(AudioUnitParameterID);
        m_parameterIDs.resize(numParams);
        if (AudioUnitGetProperty(auInstance, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, m_parameterIDs.data(), &dataSize) == noErr) {
            m_parameterRanges.resize(numParams);
            for (uint32_t i = 0; i < numParams; ++i) {
                AudioUnitParameterInfo auInfo{};
                UInt32 auInfoSize = sizeof(auInfo);
                if (AudioUnitGetProperty(auInstance, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, m_parameterIDs[i], &auInfo, &auInfoSize) == noErr) {
                    m_parameterRanges[i].minValue = auInfo.minValue;
                    m_parameterRanges[i].maxValue = auInfo.maxValue;
                    m_parameterRanges[i].defaultValue = auInfo.defaultValue;
                    if (auInfo.cfNameString) {
                        CFRelease(auInfo.cfNameString);
                    }
                } else {
                    m_parameterRanges[i].minValue = 0.0f;
                    m_parameterRanges[i].maxValue = 1.0f;
                    m_parameterRanges[i].defaultValue = 0.5f;
                }
            }
            AUListenerCreate([](void *inCallbackRefCon, void *inObject, const AudioUnitParameter *inParameter, Float32 inValue) {
                (void)inObject;
                AUPluginImpl* self = static_cast<AUPluginImpl*>(inCallbackRefCon);
                if (self && self->m_tweakedCallback) {
                    for (uint32_t i = 0; i < self->m_parameterIDs.size(); ++i) {
                        if (self->m_parameterIDs[i] == inParameter->mParameterID) {
                            const auto& r = self->m_parameterRanges[i];
                            float normalizedValue = (inValue - r.minValue) / r.range();
                            normalizedValue = std::clamp(normalizedValue, 0.0f, 1.0f);
                            self->m_tweakedCallback(i, normalizedValue);
                            break;
                        }
                    }
                }
            }, this, CFRunLoopGetMain(), kCFRunLoopDefaultMode, 0.05f, &m_auParameterListener);

            if (m_auParameterListener) {
                for (AudioUnitParameterID paramID : m_parameterIDs) {
                    AudioUnitParameter param = {auInstance, paramID, kAudioUnitScope_Global, 0};
                    AUListenerAddParameter(m_auParameterListener, nullptr, &param);
                }
            }
        }
    }
  }
#else
  (void)path;
#endif
}

AUPluginImpl::~AUPluginImpl() {
#ifdef __APPLE__
  if (m_auParameterListener) {
    AUListenerDispose(m_auParameterListener);
    m_auParameterListener = nullptr;
  }
  if (auInstance) {
    AudioUnitUninitialize(auInstance);
    AudioComponentInstanceDispose(auInstance);
  }
#endif
}

bool AUPluginImpl::getInfo(PluginInfo &outInfo) const {
#ifdef __APPLE__
  if (!auInstance) return false;
  outInfo.hasEditor = true;
  outInfo.isInstrument = m_isInstrument;
  
  // Get exact channel count by querying the StreamFormat structure on buses
  AudioStreamBasicDescription asbd;
  UInt32 size = sizeof(asbd);
  if (AudioUnitGetProperty(auInstance, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, &size) == noErr) {
      outInfo.numInputs = asbd.mChannelsPerFrame;
  } else {
      outInfo.numInputs = 2; // Default fallback
  }
  
  size = sizeof(asbd);
  if (AudioUnitGetProperty(auInstance, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &asbd, &size) == noErr) {
      outInfo.numOutputs = asbd.mChannelsPerFrame;
  } else {
      outInfo.numOutputs = 2; // Default fallback
  }
  
  outInfo.numParameters = static_cast<uint32_t>(m_parameterIDs.size());
  outInfo.latencySamples = 0;
  Float64 latencySeconds = 0.0;
  UInt32 latencySize = sizeof(latencySeconds);
  if (AudioUnitGetProperty(auInstance, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latencySeconds, &latencySize) == noErr) {
      outInfo.latencySamples = static_cast<uint32_t>(latencySeconds * 44100.0);
  }

  AudioComponent component = AudioComponentInstanceGetComponent(auInstance);
  CFStringRef cfName = nullptr;
  if (AudioComponentCopyName(component, &cfName) == noErr && cfName) {
    char buf[256];
    CFStringGetCString(cfName, buf, sizeof(buf), kCFStringEncodingUTF8);
    std::strncpy(outInfo.name, buf, sizeof(outInfo.name) - 1);
    CFRelease(cfName);
  }
  return true;
#else
  (void)outInfo;
  return false;
#endif
}

void AUPluginImpl::processAudio(float *const *inputs, uint32_t numInputs,
                                float *const *outputs, uint32_t numOutputs,
                                uint32_t numSamples, const EventData *events,
                                uint32_t numEvents,
                                EventData *outEvents, uint32_t *outCount,
                                const ProcessContext *context,
                                const bool* inputSilence) {
  if (outCount) *outCount = 0;
  (void)outEvents;
  (void)inputSilence;
#ifdef __APPLE__
  if (!auInstance) return;
 
  // Gate real-time processing during asynchronous state/soundbank loading.
  if (m_isStateLoading.load(std::memory_order_acquire)) {
      for (uint32_t ch = 0; ch < numOutputs; ++ch) {
          if (outputs[ch]) {
              std::memset(outputs[ch], 0, numSamples * sizeof(float));
          }
      }
      return;
  }
  // 1. Latch context and inputs for callbacks
  m_currentContext = context;
  m_currentInputs = inputs;
  m_currentNumInputs = numInputs;
  m_currentOutEvents = outEvents;
  m_currentOutCount = outCount;

  // 2. Handle incoming MIDI Events
  for (uint32_t i = 0; i < numEvents; ++i) {
      const auto& ev = events[i];
      if (ev.eventType == EventType::MIDI_NOTE_ON) {
          MusicDeviceMIDIEvent(auInstance, 0x90 | ev.payload.midiNote.channel, ev.payload.midiNote.pitch, ev.payload.midiNote.velocity, ev.sampleOffset);
      } else if (ev.eventType == EventType::MIDI_NOTE_OFF) {
          MusicDeviceMIDIEvent(auInstance, 0x80 | ev.payload.midiNote.channel, ev.payload.midiNote.pitch, ev.payload.midiNote.velocity, ev.sampleOffset);
      } else if (ev.eventType == EventType::MIDI_CC) {
          MusicDeviceMIDIEvent(auInstance, 0xB0 | ev.payload.midiCC.channel, ev.payload.midiCC.controllerNumber, ev.payload.midiCC.value, ev.sampleOffset);
      } else if (ev.eventType == EventType::AUTOMATION) {
          uint32_t paramIndex = ev.payload.automation.parameterIndex;
          if (paramIndex < m_parameterIDs.size()) {
              AudioUnitParameterID paramID = m_parameterIDs[paramIndex];
              const auto& r = m_parameterRanges[paramIndex];
              float denormalized = r.minValue + std::clamp(ev.payload.automation.targetValue, 0.0f, 1.0f) * r.range();
              AudioUnitSetParameter(auInstance, paramID, kAudioUnitScope_Global, 0, (AudioUnitParameterValue)denormalized, ev.sampleOffset);
          }
      }
  }

  // 3. Prepare Output Buffer List (Zero-Copy)
  size_t outListSize = sizeof(::AudioBufferList) + (std::max(1u, numOutputs) - 1) * sizeof(::CA_AudioBuffer);
  ::AudioBufferList* outputList = (::AudioBufferList*)alloca(outListSize);
  outputList->mNumberBuffers = numOutputs;
  for (uint32_t i = 0; i < numOutputs; ++i) {
    outputList->mBuffers[i].mNumberChannels = 1;
    outputList->mBuffers[i].mDataByteSize = (UInt32)(numSamples * sizeof(float));
    outputList->mBuffers[i].mData = outputs[i];
  }

  // 4. Render
  AudioUnitRenderActionFlags renderFlags = 0;
  ::AudioTimeStamp timeStamp;
  std::memset(&timeStamp, 0, sizeof(timeStamp));
  timeStamp.mSampleTime = (Float64)m_continuousSampleTime;
  timeStamp.mFlags = kAudioTimeStampSampleTimeValid;
  
  if (context) {
    timeStamp.mHostTime = context->hardwareTimestamp;
    timeStamp.mFlags |= kAudioTimeStampHostTimeValid;
  }
  
  m_continuousSampleTime += numSamples;

  {
      Layer1::ScopedDenormalHandler denormalHandler;
      AudioUnitRender(auInstance, &renderFlags, &timeStamp, 0, numSamples, outputList);
  }

  m_currentOutEvents = nullptr;
  m_currentOutCount = nullptr;
#else
  (void)inputs; (void)numInputs; (void)outputs; (void)numOutputs;
  (void)numSamples; (void)events; (void)numEvents; (void)context;
#endif
}

#ifdef __APPLE__
OSStatus AUPluginImpl::inputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const ::AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, ::AudioBufferList *ioData) {
    (void)ioActionFlags; (void)inTimeStamp; (void)inBusNumber; (void)inNumberFrames;
    AUPluginImpl* self = static_cast<AUPluginImpl*>(inRefCon);
    if (!self || !self->m_currentInputs) return kAudioUnitErr_NoConnection;

    for (UInt32 i = 0; i < ioData->mNumberBuffers; ++i) {
        if (i < self->m_currentNumInputs) {
            ioData->mBuffers[i].mData = const_cast<float*>(self->m_currentInputs[i]);
        } else {
            // Fill with silence if we don't have enough inputs
            if (ioData->mBuffers[i].mData) {
                std::memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
            }
        }
    }
    return noErr;
}

OSStatus AUPluginImpl::beatAndTempoProc(void *inHostUserData, Float64 *outCurrentBeat, Float64 *outCurrentTempo) {
    AUPluginImpl* self = static_cast<AUPluginImpl*>(inHostUserData);
    if (!self || !self->m_currentContext) return kAudioUnitErr_InvalidParameter;
    const auto& ctx = *self->m_currentContext;
    
    if (outCurrentBeat) *outCurrentBeat = (Float64)ctx.transport.beat - 1.0 + ((Float64)ctx.transport.tick / (Float64)ctx.transport.ticksPerBeat);
    if (outCurrentTempo) *outCurrentTempo = ctx.transport.bpm;
    return noErr;
}

OSStatus AUPluginImpl::musicalTimeLocationProc(void *inHostUserData, UInt32 *outDeltaSampleOffsetToNextBeat, Float32 *outTimeSigNumerator, UInt32 *outTimeSigDenominator, Float64 *outCurrentMeasureDownbeat) {
    (void)outDeltaSampleOffsetToNextBeat;
    (void)outCurrentMeasureDownbeat;
    AUPluginImpl* self = static_cast<AUPluginImpl*>(inHostUserData);
    if (!self || !self->m_currentContext) return kAudioUnitErr_InvalidParameter;
    const auto& ctx = *self->m_currentContext;

    if (outTimeSigNumerator) *outTimeSigNumerator = (Float32)ctx.transport.numerator;
    if (outTimeSigDenominator) *outTimeSigDenominator = (UInt32)ctx.transport.denominator;
    return noErr;
}

OSStatus AUPluginImpl::transportStateProc(void *inHostUserData, Boolean *outIsPlaying, Boolean *outTransportStateChanged, Float64 *outSampleTickPosition, Boolean *outIsCycling, Float64 *outCycleStartBeat, Float64 *outCycleEndBeat) {
    AUPluginImpl* self = static_cast<AUPluginImpl*>(inHostUserData);
    if (!self || !self->m_currentContext) return kAudioUnitErr_InvalidParameter;
    const auto& ctx = *self->m_currentContext;

    if (outIsPlaying) *outIsPlaying = (self->m_currentContext->transportState == TransportState::PLAYING);
    if (outTransportStateChanged) *outTransportStateChanged = false; // Simplified
    if (outSampleTickPosition) *outSampleTickPosition = (Float64)ctx.transport.positionSample;
    if (outIsCycling) *outIsCycling = false;
    if (outCycleStartBeat) *outCycleStartBeat = 0.0;
    if (outCycleEndBeat) *outCycleEndBeat = 0.0;

    return noErr;
}

void AUPluginImpl::queueMIDIOutputForNextBuffer(const ::MIDIPacketList *pktlist) {
    if (!pktlist || !m_currentOutEvents || !m_currentOutCount) return;
    const MIDIPacket* packet = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
        if (packet->length > 0) {
            UInt8 status = packet->data[0] & 0xF0;
            UInt8 channel = packet->data[0] & 0x0F;
            
            EventData ev;
            std::memset(&ev, 0, sizeof(ev));
            ev.sampleOffset = 0; // Trigger immediately next block
            ev.targetNodeId = {}; 
            
            bool gotEvent = false;
            if (status == 0x90 && packet->length >= 3) {
                if (packet->data[2] > 0) {
                    ev.eventType = EventType::MIDI_NOTE_ON;
                    ev.payload.midiNote.channel = channel;
                    ev.payload.midiNote.pitch = packet->data[1];
                    ev.payload.midiNote.velocity = packet->data[2];
                    gotEvent = true;
                } else {
                    ev.eventType = EventType::MIDI_NOTE_OFF;
                    ev.payload.midiNote.channel = channel;
                    ev.payload.midiNote.pitch = packet->data[1];
                    ev.payload.midiNote.velocity = 0;
                    gotEvent = true;
                }
            } else if (status == 0x80 && packet->length >= 3) {
                ev.eventType = EventType::MIDI_NOTE_OFF;
                ev.payload.midiNote.channel = channel;
                ev.payload.midiNote.pitch = packet->data[1];
                ev.payload.midiNote.velocity = packet->data[2];
                gotEvent = true;
            } else if (status == 0xB0 && packet->length >= 3) {
                ev.eventType = EventType::MIDI_CC;
                ev.payload.midiCC.channel = channel;
                ev.payload.midiCC.controllerNumber = packet->data[1];
                ev.payload.midiCC.value = packet->data[2];
                gotEvent = true;
            }
            
            if (gotEvent && *m_currentOutCount < 512) {
                m_currentOutEvents[*m_currentOutCount] = ev;
                (*m_currentOutCount)++;
            }
        }
        packet = MIDIPacketNext(packet);
    }
}

OSStatus AUPluginImpl::midiOutputCallback(void *userData, const ::AudioTimeStamp *timeStamp, UInt32 midiOutNum, const ::MIDIPacketList *pktlist) {
    (void)timeStamp; (void)midiOutNum;
    AUPluginImpl* self = static_cast<AUPluginImpl*>(userData);
    if (self) {
        self->queueMIDIOutputForNextBuffer(pktlist);
    }
    return noErr;
}

#endif

float AUPluginImpl::getParameterValue(uint32_t paramIndex) const {
  (void)paramIndex;
#ifdef __APPLE__
  if (!auInstance || paramIndex >= m_parameterIDs.size()) return 0.0f;
  AudioUnitParameterID paramID = m_parameterIDs[paramIndex];
  AudioUnitParameterValue value = 0.0f;
  AudioUnitGetParameter(auInstance, paramID, kAudioUnitScope_Global, 0, &value);
  
  const auto& r = m_parameterRanges[paramIndex];
  return std::clamp((static_cast<float>(value) - r.minValue) / r.range(), 0.0f, 1.0f);
#else
  return 0.0f;
#endif
}

void AUPluginImpl::setParameterValue(uint32_t paramIndex, float value) {
  (void)paramIndex; (void)value;
#ifdef __APPLE__
  if (!auInstance || paramIndex >= m_parameterIDs.size()) return;
  AudioUnitParameterID paramID = m_parameterIDs[paramIndex];
  const auto& r = m_parameterRanges[paramIndex];
  float denormalized = r.minValue + std::clamp(value, 0.0f, 1.0f) * r.range();
  if (m_auParameterListener) {
      AudioUnitParameter param = {auInstance, paramID, kAudioUnitScope_Global, 0};
      AUParameterSet(m_auParameterListener, this, &param, (AudioUnitParameterValue)denormalized, 0);
  } else {
      AudioUnitSetParameter(auInstance, paramID, kAudioUnitScope_Global, 0, (AudioUnitParameterValue)denormalized, 0);
  }
#endif
}

bool AUPluginImpl::getParameterInfo(uint32_t paramIndex, ::ParameterInfo &outInfo) const {
  (void)paramIndex; (void)outInfo;
#ifdef __APPLE__
  if (!auInstance || paramIndex >= m_parameterIDs.size()) return false;
  
  AudioUnitParameterID paramID = m_parameterIDs[paramIndex];
  outInfo.index = paramIndex;
  
  AudioUnitParameterInfo auInfo{};
  UInt32 auInfoSize = sizeof(auInfo);
  if (AudioUnitGetProperty(auInstance, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, paramID, &auInfo, &auInfoSize) != noErr) {
    return false;
  }
  
  outInfo.name[0] = '\0';
  if (auInfo.cfNameString) {
    CFStringGetCString(auInfo.cfNameString, outInfo.name, sizeof(outInfo.name), kCFStringEncodingUTF8);
    CFRelease(auInfo.cfNameString);
  } else if (auInfo.name) {
    std::strncpy(outInfo.name, auInfo.name, sizeof(outInfo.name) - 1);
    outInfo.name[sizeof(outInfo.name) - 1] = '\0';
  }
  
  outInfo.unit[0] = '\0';
  switch (auInfo.unit) {
    case kAudioUnitParameterUnit_Decibels: std::strcpy(outInfo.unit, "dB"); break;
    case kAudioUnitParameterUnit_Hertz: std::strcpy(outInfo.unit, "Hz"); break;
    case kAudioUnitParameterUnit_Seconds: std::strcpy(outInfo.unit, "s"); break;
    case kAudioUnitParameterUnit_Percent: std::strcpy(outInfo.unit, "%"); break;
    case kAudioUnitParameterUnit_BPM: std::strcpy(outInfo.unit, "BPM"); break;
    default: break;
  }
  
  outInfo.minValue = 0.0f;
  if (auInfo.unit == kAudioUnitParameterUnit_Indexed) {
      outInfo.maxValue = static_cast<float>(auInfo.maxValue - auInfo.minValue);
  } else {
      outInfo.maxValue = 1.0f;
  }
  const auto& r = m_parameterRanges[paramIndex];
  outInfo.defaultValue = std::clamp((r.defaultValue - r.minValue) / r.range(), 0.0f, 1.0f);
  
  outInfo.flags = ::ParameterInfo::NONE;
  if (auInfo.flags & kAudioUnitParameterFlag_IsWritable) {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_AUTOMATABLE);
  } else {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_READ_ONLY);
  }
  if (auInfo.unit == kAudioUnitParameterUnit_Boolean) {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_BOOLEAN);
  } else if (auInfo.unit == kAudioUnitParameterUnit_Indexed) {
    outInfo.flags = static_cast<::ParameterInfo::Flags>(outInfo.flags | ::ParameterInfo::IS_INTEGER);
  }
  return true;
#else
  return false;
#endif
}

std::vector<uint8_t> AUPluginImpl::getState() const { 
#ifdef __APPLE__
    if (!auInstance) return {};
    CFPropertyListRef propList = nullptr;
    UInt32 dataSize = sizeof(propList);
    if (AudioUnitGetProperty(auInstance, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &propList, &dataSize) != noErr) return {};
    
    CFDataRef data = CFPropertyListCreateData(kCFAllocatorDefault, propList, kCFPropertyListBinaryFormat_v1_0, 0, nullptr);
    CFRelease(propList);
    if (!data) return {};
    
    uint64_t actualSize = (uint64_t)CFDataGetLength(data);
    std::vector<uint8_t> buffer(actualSize);
    std::memcpy(buffer.data(), CFDataGetBytePtr(data), actualSize);
    
    CFRelease(data);
    return buffer;
#else
    return {};
#endif
}

bool AUPluginImpl::loadState(const uint8_t *buffer, uint64_t bufferSize) {
#ifdef __APPLE__
  if (!auInstance) return false;

  CFDataRef data = CFDataCreate(kCFAllocatorDefault, buffer, (CFIndex)bufferSize);
  if (!data) return false;

  auto loadTask = [this, data]() {
    CFPropertyListRef propList = CFPropertyListCreateWithData(kCFAllocatorDefault, data, kCFPropertyListImmutable, nullptr, nullptr);
    if (propList) {
        AudioUnitSetProperty(auInstance, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &propList, sizeof(propList));
        CFRelease(propList);
    }
    CFRelease(data);
    m_isStateLoading = false;
  };

  if (m_butler) {
    m_isStateLoading = true;
    m_butler->scheduleTask(loadTask);
    return true;
  } else {
    loadTask();
    return true;
  }
#else
  (void)buffer; (void)bufferSize;
  return false;
#endif
}

bool AUPluginImpl::openEditor(void *parentWindow, int &outWidth, int &outHeight) {
#ifdef __APPLE__
  if (!auInstance) return false;
  
  NSView* parentView = (__bridge NSView*)parentWindow;
  if (!parentView) return false;

  // 1. Get Cocoa UI View Info
  AudioUnitCocoaViewInfo cocoaViewInfo;
  std::memset(&cocoaViewInfo, 0, sizeof(cocoaViewInfo));
  UInt32 size = sizeof(cocoaViewInfo);
  OSStatus status = AudioUnitGetProperty(auInstance,
                                         kAudioUnitProperty_CocoaUI,
                                         kAudioUnitScope_Global,
                                         0,
                                         &cocoaViewInfo,
                                         &size);
  if (status != noErr) return false;

  // 2. Load View Factory Bundle from URL
  NSURL* bundleURL = (__bridge NSURL*)cocoaViewInfo.mCocoaAUViewBundleLocation;
  NSBundle* viewBundle = [NSBundle bundleWithURL:bundleURL];
  if (!viewBundle) {
      if (cocoaViewInfo.mCocoaAUViewBundleLocation) CFRelease(cocoaViewInfo.mCocoaAUViewBundleLocation);
      if (cocoaViewInfo.mCocoaAUViewClass[0]) CFRelease(cocoaViewInfo.mCocoaAUViewClass[0]);
      return false;
  }

  // 3. Find UI Factory Class Name
  NSString* className = (__bridge NSString*)cocoaViewInfo.mCocoaAUViewClass[0];
  Class factoryClass = [viewBundle classNamed:className];
  if (!factoryClass) {
      if (cocoaViewInfo.mCocoaAUViewBundleLocation) CFRelease(cocoaViewInfo.mCocoaAUViewBundleLocation);
      if (cocoaViewInfo.mCocoaAUViewClass[0]) CFRelease(cocoaViewInfo.mCocoaAUViewClass[0]);
      return false;
  }

  id<AUCocoaUIBase> factoryInstance = [[factoryClass alloc] init];
  if (!factoryInstance) {
      if (cocoaViewInfo.mCocoaAUViewBundleLocation) CFRelease(cocoaViewInfo.mCocoaAUViewBundleLocation);
      if (cocoaViewInfo.mCocoaAUViewClass[0]) CFRelease(cocoaViewInfo.mCocoaAUViewClass[0]);
      return false;
  }

  // 4. Retrieve Native NSView instance
  NSSize viewSize = NSMakeSize(600, 400); // Standard starting bounds
  NSView* pluginView = [factoryInstance uiViewForAudioUnit:auInstance withSize:viewSize];

  // Clean up CF references
  if (cocoaViewInfo.mCocoaAUViewBundleLocation) CFRelease(cocoaViewInfo.mCocoaAUViewBundleLocation);
  if (cocoaViewInfo.mCocoaAUViewClass[0]) CFRelease(cocoaViewInfo.mCocoaAUViewClass[0]);

  if (pluginView) {
      outWidth = static_cast<int>(pluginView.frame.size.width);
      outHeight = static_cast<int>(pluginView.frame.size.height);
      if (outWidth <= 0 || outHeight <= 0) {
          outWidth = 600;
          outHeight = 400;
      }

      [pluginView setWantsLayer:YES];
      // Dispatch UI update to the macOS main run loop for safety
      dispatch_async(dispatch_get_main_queue(), ^{
          [parentView addSubview:pluginView];
          [pluginView setFrame:parentView.bounds];
          [pluginView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
          
          // Create a lightweight native timer to continuously pump the display cycle
          NSTimer* timer = [NSTimer scheduledTimerWithTimeInterval:1.0/60.0 
                                                           repeats:YES 
                                                              block:^(NSTimer * _Nonnull t) {
              if ([pluginView superview]) {
                  // Force the runloop to commit JUCE's optimized dirty rectangles
                  // without dirtying the entire view ourselves.
                  [CATransaction flush];
              } else {
                  // Automatically clean up the timer when the window is closed
                  [t invalidate]; 
              }
          }];
          [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
      });
      return true;
  }
  return false;
#else
  (void)parentWindow;
  return false;
#endif
}

void AUPluginImpl::closeEditor() {
}

} // namespace Layer3
