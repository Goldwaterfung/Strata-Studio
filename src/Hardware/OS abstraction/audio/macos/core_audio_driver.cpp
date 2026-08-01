// core_audio_driver.cpp
// Layer 1: Hardware/OS Abstraction - macOS Core Audio Driver Implementation

#include "core_audio_driver.h"
#include "../audio_format_converter.h"
#include "../audio_utils.h"
#include "../audio_driver_defaults.h"

// macOS Platform Headers (Quarantined)
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <vector>
#include <atomic>
#include <cstring>
#include <algorithm>

namespace Layer1 {

// =============================================================================
// INTERNAL IMPLEMENTATION (PIMPL)
// =============================================================================

class CoreAudioDriver::Impl {
public:
    Impl()
        : currentState(StreamState::IDLE)
        , audioUnit(nullptr)
        , deviceID(kAudioDeviceUnknown)
        , inputBufferList(nullptr)
        , workgroupHandle(WorkgroupHandle::invalid())
    {
    }
    
    WorkgroupHandle workgroupHandle;

    ~Impl() {
        cleanup();
    }

    std::atomic<StreamState> currentState;
    IAudioDriver::StreamConfig currentConfig;
    AudioComponentInstance audioUnit;
    AudioDeviceID deviceID;

    // Buffers for input capture (Core Audio uses AudioBufferList)
    AudioBufferList* inputBufferList;
    std::vector<float*> inputPlanarBuffers;
    std::vector<std::vector<float>> inputPlanarData;
    
    // Buffers for output (passed to Layer 3)
    std::vector<float*> outputPlanarBuffers;
    std::vector<std::vector<float>> outputPlanarData;

    void cleanup() {
        unregisterListeners();
        if (audioUnit) {
            AudioOutputUnitStop(audioUnit);
            AudioUnitUninitialize(audioUnit);
            AudioComponentInstanceDispose(audioUnit);
            audioUnit = nullptr;
        }
        
        if (inputBufferList) {
            free(inputBufferList);
            inputBufferList = nullptr;
        }
    }

    static OSStatus renderCallback(void* inRefCon,
                                  AudioUnitRenderActionFlags* ioActionFlags,
                                  const AudioTimeStamp* inTimeStamp,
                                  UInt32 inBusNumber,
                                  UInt32 inNumberFrames,
                                  AudioBufferList* ioData) {
        ScopedDenormalHandler denormalHandler;
        Impl* impl = static_cast<Impl*>(inRefCon);
        
        // 1. Phase 1: Start Cycle & Input Capture
        if (impl->currentConfig.client) {
            impl->currentConfig.client->startCycle(inTimeStamp->mHostTime, inNumberFrames);
        }

        float* const* inputChannels = nullptr;
        if (impl->currentConfig.inputDeviceIndex != UNUSED_DEVICE_INDEX) {
            OSStatus err = AudioUnitRender(impl->audioUnit, ioActionFlags, inTimeStamp, 1, inNumberFrames, impl->inputBufferList);
            if (err == noErr) {
                inputChannels = impl->inputPlanarBuffers.data();
            }
        }

        // 2. Phase 2: Processing (with Sub-Cycle Splitting capability)
        uint32_t numOutputChannels = ioData->mNumberBuffers;
        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            std::fill(impl->outputPlanarData[ch].begin(), impl->outputPlanarData[ch].end(), 0.0f);
            impl->outputPlanarBuffers[ch] = impl->outputPlanarData[ch].data();
        }

        if (impl->currentConfig.client) {
            impl->currentConfig.client->processAudio(
                inputChannels,
                impl->currentConfig.inputDeviceIndex != UNUSED_DEVICE_INDEX ? impl->inputPlanarBuffers.size() : 0,
                impl->outputPlanarBuffers.data(),
                numOutputChannels,
                inNumberFrames
            );
        }

        // 3. Phase 3: End Cycle & Copy Data
        if (impl->currentConfig.client) {
            impl->currentConfig.client->endCycle(inNumberFrames);
        }

        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            float* dest = static_cast<float*>(ioData->mBuffers[ch].mData);
            std::memcpy(dest, impl->outputPlanarBuffers[ch], inNumberFrames * sizeof(float));
        }

        return noErr;
    }

    static OSStatus propertyListener(AudioObjectID inObjectID,
                                   UInt32 inNumberAddresses,
                                   const AudioObjectPropertyAddress* inAddresses,
                                   void* inRefCon) {
        Impl* impl = static_cast<Impl*>(inRefCon);
        
        for (UInt32 i = 0; i < inNumberAddresses; ++i) {
            switch (inAddresses[i].mSelector) {
                case kAudioDevicePropertyNominalSampleRate: {
                    Float64 newRate = 0;
                    UInt32 size = sizeof(newRate);
                    AudioObjectGetPropertyData(inObjectID, &inAddresses[i], 0, nullptr, &size, &newRate);
                    if (impl->currentConfig.client) {
                        impl->currentConfig.client->onSampleRateChanged(static_cast<uint32_t>(newRate));
                    }
                    break;
                }
                case kAudioDeviceProcessorOverload: {
                    if (impl->currentConfig.client) {
                        impl->currentConfig.client->onXrun();
                    }
                    break;
                }
                case kAudioDevicePropertyDeviceIsAlive: {
                    if (impl->currentConfig.client) {
                        // Handle device disconnection
                        impl->currentConfig.client->onDeviceDisconnected();
                    }
                    break;
                }
            }
        }
        return noErr;
    }

    void registerListeners() {
        if (deviceID == kAudioDeviceUnknown) return;

        AudioObjectPropertyAddress addresses[] = {
            { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
            { kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
            { kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain }
        };

        AudioObjectAddPropertyListenerBlock(deviceID, &addresses[0], nullptr, ^(UInt32 n, const AudioObjectPropertyAddress* addr) {
            propertyListener(deviceID, n, addr, this);
        });
        AudioObjectAddPropertyListenerBlock(deviceID, &addresses[1], nullptr, ^(UInt32 n, const AudioObjectPropertyAddress* addr) {
            propertyListener(deviceID, n, addr, this);
        });
        AudioObjectAddPropertyListenerBlock(deviceID, &addresses[2], nullptr, ^(UInt32 n, const AudioObjectPropertyAddress* addr) {
            propertyListener(deviceID, n, addr, this);
        });
    }

    void unregisterListeners() {
        if (deviceID == kAudioDeviceUnknown) return;
        
        AudioObjectPropertyAddress addresses[] = {
            { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
            { kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
            { kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain }
        };

        // Note: Using the simple version for deregistration as Block-based deregistration is more complex
        AudioObjectRemovePropertyListener(deviceID, &addresses[0], propertyListener, this);
        AudioObjectRemovePropertyListener(deviceID, &addresses[1], propertyListener, this);
        AudioObjectRemovePropertyListener(deviceID, &addresses[2], propertyListener, this);
    }

    OpenResult makeErrorResult(StreamError error, const char* message) {
        OpenResult result;
        result.success = false;
        result.error = error;
        std::strncpy(result.errorMessage, message, sizeof(result.errorMessage) - 1);
        result.errorMessage[sizeof(result.errorMessage) - 1] = '\0';
        return result;
    }
};

// =============================================================================
// CoreAudioDriver WRAPPER METHODS
// =============================================================================

CoreAudioDriver::CoreAudioDriver() : pImpl(std::make_unique<Impl>()) {}
CoreAudioDriver::~CoreAudioDriver() = default;

OpenResult CoreAudioDriver::openStream(const StreamConfig& config) {
    if (pImpl->currentState != StreamState::IDLE) {
        return pImpl->makeErrorResult(StreamError::INVALID_CONFIGURATION, "Stream already open");
    }

    pImpl->currentConfig = config;

    // 1. Find the HAL Output Component
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) return pImpl->makeErrorResult(StreamError::SYSTEM_ERROR, "Could not find Core Audio HAL component");

    OSStatus err = AudioComponentInstanceNew(comp, &pImpl->audioUnit);
    if (err != noErr) return pImpl->makeErrorResult(StreamError::SYSTEM_ERROR, "Failed to create AudioUnit instance");

    // 2. Enable I/O
    UInt32 enableIO = 1;
    UInt32 disableIO = 0;

    // Output is bus 0
    if (config.outputDeviceIndex != UNUSED_DEVICE_INDEX) {
        err = AudioUnitSetProperty(pImpl->audioUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enableIO, sizeof(enableIO));
    } else {
        err = AudioUnitSetProperty(pImpl->audioUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &disableIO, sizeof(disableIO));
    }

    // Input is bus 1
    if (config.inputDeviceIndex != UNUSED_DEVICE_INDEX) {
        err = AudioUnitSetProperty(pImpl->audioUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));
    }

    // 3. Set Device
    AudioDeviceID devID = kAudioDeviceUnknown;
    
    if (config.outputDeviceIndex != UNUSED_DEVICE_INDEX) {
        // Enumerate to find the device ID for the given index
        AudioObjectPropertyAddress addr = {
            kAudioHardwarePropertyDevices,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        
        UInt32 size = 0;
        AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size);
        uint32_t count = size / sizeof(AudioDeviceID);
        
        if (config.outputDeviceIndex < count) {
            std::vector<AudioDeviceID> devices(count);
            AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, devices.data());
            devID = devices[config.outputDeviceIndex];
        } else {
            // Fallback to default
            UInt32 devSize = sizeof(AudioDeviceID);
            AudioObjectPropertyAddress defaultAddr = {
                kAudioHardwarePropertyDefaultOutputDevice,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
            };
            AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, nullptr, &devSize, &devID);
        }
    }
    
    if (devID != kAudioDeviceUnknown) {
        err = AudioUnitSetProperty(pImpl->audioUnit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &devID, sizeof(AudioDeviceID));
    }

    // 4. Set Stream Format (Planar Float32)
    AudioStreamBasicDescription streamFormat;
    streamFormat.mSampleRate = config.sampleRate;
    streamFormat.mFormatID = kAudioFormatLinearPCM;
    streamFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagIsPacked;
    streamFormat.mBytesPerPacket = sizeof(float);
    streamFormat.mFramesPerPacket = 1;
    streamFormat.mBytesPerFrame = sizeof(float);
    streamFormat.mBitsPerChannel = 32;

    // Set format for output (bus 0, input scope)
    if (config.outputDeviceIndex != UNUSED_DEVICE_INDEX) {
        streamFormat.mChannelsPerFrame = config.numOutputChannels;
        err = AudioUnitSetProperty(pImpl->audioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &streamFormat, sizeof(streamFormat));
        
        // Pre-allocate output buffers
        pImpl->outputPlanarData.resize(config.numOutputChannels);
        pImpl->outputPlanarBuffers.resize(config.numOutputChannels);
        for (uint32_t i = 0; i < config.numOutputChannels; ++i) {
            pImpl->outputPlanarData[i].resize(config.bufferSize);
            pImpl->outputPlanarBuffers[i] = pImpl->outputPlanarData[i].data();
        }
    }

    // Set format for input (bus 1, output scope)
    if (config.inputDeviceIndex != UNUSED_DEVICE_INDEX) {
        streamFormat.mChannelsPerFrame = config.numInputChannels;
        err = AudioUnitSetProperty(pImpl->audioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &streamFormat, sizeof(streamFormat));
        
        // Allocate Input Buffer List
        pImpl->inputBufferList = (AudioBufferList*)malloc(sizeof(AudioBufferList) + (config.numInputChannels - 1) * sizeof(AudioBuffer));
        pImpl->inputBufferList->mNumberBuffers = config.numInputChannels;
        pImpl->inputPlanarData.resize(config.numInputChannels);
        pImpl->inputPlanarBuffers.resize(config.numInputChannels);
        for (uint32_t i = 0; i < config.numInputChannels; ++i) {
            pImpl->inputPlanarData[i].resize(config.bufferSize);
            pImpl->inputPlanarBuffers[i] = pImpl->inputPlanarData[i].data();
            pImpl->inputBufferList->mBuffers[i].mNumberChannels = 1;
            pImpl->inputBufferList->mBuffers[i].mDataByteSize = config.bufferSize * sizeof(float);
            pImpl->inputBufferList->mBuffers[i].mData = pImpl->inputPlanarBuffers[i];
        }
    }

    // 5. Set Callback
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = Impl::renderCallback;
    callbackStruct.inputProcRefCon = pImpl.get();
    err = AudioUnitSetProperty(pImpl->audioUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callbackStruct, sizeof(callbackStruct));

    err = AudioUnitInitialize(pImpl->audioUnit);
    if (err != noErr) return pImpl->makeErrorResult(StreamError::SYSTEM_ERROR, "Failed to initialize AudioUnit");

    // 7. Retrieve Workgroup (macOS 11.0+)
    os_workgroup_t workgroup = nullptr;
    UInt32 wgSize = sizeof(os_workgroup_t);
    AudioUnitGetProperty(pImpl->audioUnit, 
                        kAudioDevicePropertyIOThreadOSWorkgroup, 
                        kAudioUnitScope_Global, 
                        0, 
                        &workgroup, 
                        &wgSize);
    
    if (workgroup) {
        pImpl->workgroupHandle = { reinterpret_cast<uintptr_t>(workgroup), 1 };
    }

    pImpl->registerListeners();

    pImpl->currentState = StreamState::OPEN;
    return OpenResult{.success = true};
}

bool CoreAudioDriver::startStream() {
    if (pImpl->currentState != StreamState::OPEN) return false;
    OSStatus err = AudioOutputUnitStart(pImpl->audioUnit);
    if (err != noErr) return false;
    pImpl->currentState = StreamState::RUNNING;
    return true;
}

bool CoreAudioDriver::stopStream() {
    if (pImpl->currentState != StreamState::RUNNING) return false;
    OSStatus err = AudioOutputUnitStop(pImpl->audioUnit);
    if (err != noErr) return false;
    pImpl->currentState = StreamState::OPEN;
    return true;
}

void CoreAudioDriver::closeStream() {
    pImpl->cleanup();
    pImpl->currentState = StreamState::IDLE;
}

StreamState CoreAudioDriver::getState() const {
    return pImpl->currentState;
}

WorkgroupHandle CoreAudioDriver::getWorkgroupHandle() const {
    return pImpl->workgroupHandle;
}

uint32_t CoreAudioDriver::getDeviceCount() const {
    // Basic enumeration using AudioHardware
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size);
    return size / sizeof(AudioDeviceID);
}

DeviceInfo CoreAudioDriver::getDeviceInfo(uint32_t deviceIndex) const {
    DeviceInfo info = {};
    
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size);
    uint32_t count = size / sizeof(AudioDeviceID);
    
    if (deviceIndex >= count) return info;
    
    std::vector<AudioDeviceID> devices(count);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, devices.data());
    
    AudioDeviceID devID = devices[deviceIndex];
    
    // Get Name
    size = sizeof(info.name);
    addr.mSelector = kAudioObjectPropertyName;
    AudioObjectGetPropertyData(devID, &addr, 0, nullptr, &size, info.name);
    
    // Get Manufacturer
    size = sizeof(info.manufacturer);
    addr.mSelector = kAudioObjectPropertyManufacturer;
    AudioObjectGetPropertyData(devID, &addr, 0, nullptr, &size, info.manufacturer);
    
    // Get Channels
    addr.mSelector = kAudioDevicePropertyStreamConfiguration;
    addr.mScope = kAudioDevicePropertyScopeOutput;
    AudioObjectGetPropertyDataSize(devID, &addr, 0, nullptr, &size);
    AudioBufferList* list = (AudioBufferList*)malloc(size);
    AudioObjectGetPropertyData(devID, &addr, 0, nullptr, &size, list);
    for (uint32_t i = 0; i < list->mNumberBuffers; ++i) info.maxOutputChannels += list->mBuffers[i].mNumberChannels;
    free(list);
    
    addr.mScope = kAudioDevicePropertyScopeInput;
    AudioObjectGetPropertyDataSize(devID, &addr, 0, nullptr, &size);
    list = (AudioBufferList*)malloc(size);
    AudioObjectGetPropertyData(devID, &addr, 0, nullptr, &size, list);
    for (uint32_t i = 0; i < list->mNumberBuffers; ++i) info.maxInputChannels += list->mBuffers[i].mNumberChannels;
    free(list);
    
    // Get Default Sample Rate
    size = sizeof(Float64);
    addr.mSelector = kAudioDevicePropertyNominalSampleRate;
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    Float64 sampleRate = 0;
    if (AudioObjectGetPropertyData(devID, &addr, 0, nullptr, &size, &sampleRate) == noErr) {
        info.defaultSampleRate = static_cast<uint32_t>(sampleRate);
    } else {
        info.defaultSampleRate = AudioDefaults::SAMPLE_RATE;
    }
    
    // Get Supported Sample Rates
    addr.mSelector = kAudioDevicePropertyAvailableNominalSampleRates;
    if (AudioObjectGetPropertyDataSize(devID, &addr, 0, nullptr, &size) == noErr) {
        uint32_t numRates = size / sizeof(AudioValueRange);
        std::vector<AudioValueRange> rates(numRates);
        if (AudioObjectGetPropertyData(devID, &addr, 0, nullptr, &size, rates.data()) == noErr) {
            info.numSampleRates = 0;
            for (uint32_t i = 0; i < numRates && info.numSampleRates < MAX_SUPPORTED_SAMPLE_RATES; ++i) {
                // AudioValueRange can be a range, but for sample rates it's often fixed values (min == max)
                info.supportedSampleRates[info.numSampleRates++] = static_cast<uint32_t>(rates[i].mMinimum);
            }
        }
    }
    
    if (info.numSampleRates == 0) {
        info.numSampleRates = 1;
        info.supportedSampleRates[0] = info.defaultSampleRate;
    }
    
    info.preferredBufferSize = AudioDefaults::BUFFER_SIZE; // Standard default
    
    // Check if default
    AudioDeviceID defaultOutID = kAudioDeviceUnknown;
    size = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress defaultAddr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, nullptr, &size, &defaultOutID) == noErr) {
        info.isDefaultOutput = (devID == defaultOutID);
    }
    
    AudioDeviceID defaultInID = kAudioDeviceUnknown;
    defaultAddr.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, nullptr, &size, &defaultInID) == noErr) {
        info.isDefaultInput = (devID == defaultInID);
    }
    
    return info;
}

} // namespace Layer1
