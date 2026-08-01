// rt_audio_wrapper.cpp
// Layer 1: Hardware/OS Abstraction - RtAudio Wrapper Implementation

#include "rt_audio_wrapper.h"

#ifdef HAVE_RTAUDIO

#include "audio_format_converter.h"
#include "audio_utils.h"
#include "audio_driver_defaults.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#include <rtaudio/RtAudio.h>
#pragma clang diagnostic pop
#include <cstring>
#include <algorithm>
#include <vector>
#include <mutex>

namespace Layer1 {


namespace {
RtAudio::Api toRtAudioApi(AudioAPI api) {
    switch (api) {
        case AudioAPI::ASIO:         return RtAudio::WINDOWS_ASIO;
        case AudioAPI::WASAPI:       return RtAudio::WINDOWS_WASAPI;
        case AudioAPI::CORE_AUDIO:   return RtAudio::MACOSX_CORE;
        case AudioAPI::DIRECT_SOUND: return RtAudio::WINDOWS_DS;
        default:                     return RtAudio::UNSPECIFIED;
    }
}

StreamError mapRtAudioError(RtAudioErrorType rtError) {
    switch (rtError) {
        case RTAUDIO_NO_ERROR:           return StreamError::NONE;
        case RTAUDIO_NO_DEVICES_FOUND:   return StreamError::DEVICE_NOT_FOUND;
        case RTAUDIO_INVALID_DEVICE:     return StreamError::DEVICE_NOT_FOUND;
        case RTAUDIO_MEMORY_ERROR:       return StreamError::SYSTEM_ERROR;
        case RTAUDIO_INVALID_PARAMETER:  return StreamError::INVALID_CONFIGURATION;
        case RTAUDIO_INVALID_USE:        return StreamError::DEVICE_IN_USE;
        default:                         return StreamError::SYSTEM_ERROR;
    }
}
} // namespace

class RtAudioWrapper::RtAudioImpl {
public:
    std::unique_ptr<RtAudio> audio;
    RtAudio::StreamParameters inputParams;
    RtAudio::StreamParameters outputParams;
    RtAudio::StreamOptions options;
    unsigned int bufferFrames;
    bool streamOpen;
    std::vector<unsigned int> deviceIds;

    explicit RtAudioImpl(RtAudio::Api api)
        : audio(std::make_unique<RtAudio>(api))
        , bufferFrames(0)
        , streamOpen(false)
    {
        options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_SCHEDULE_REALTIME;
        options.numberOfBuffers = AudioDefaults::NUM_BUFFERS;
        std::memset(&inputParams, 0, sizeof(inputParams));
        std::memset(&outputParams, 0, sizeof(outputParams));
        refreshDeviceIds();
    }

    void refreshDeviceIds() {
        try {
            deviceIds = audio->getDeviceIds();
        } catch (...) {
            deviceIds.clear();
        }
    }

    ~RtAudioImpl() {
        if (streamOpen && audio->isStreamOpen()) {
            try { audio->closeStream(); } catch (...) {}
        }
    }
};

RtAudioWrapper::RtAudioWrapper(AudioAPI api)
    : currentState(StreamState::IDLE)
    , requestedApi(api)
    , clientCallback(nullptr)
{
    currentConfig.inputDeviceIndex = UNUSED_DEVICE_INDEX;
    currentConfig.outputDeviceIndex = UNUSED_DEVICE_INDEX;
    currentConfig.numInputChannels = 0;
    currentConfig.numOutputChannels = 0;
    currentConfig.sampleRate = 0;
    currentConfig.bufferSize = 0;
    currentConfig.client = nullptr;
}

RtAudioWrapper::~RtAudioWrapper() {
    closeStream();
}

void RtAudioWrapper::ensureImplInitialized() const {
    std::lock_guard<std::mutex> lock(implMutex);
    if (!impl) {
        impl = std::make_unique<RtAudioImpl>(toRtAudioApi(requestedApi));
    }
}

OpenResult RtAudioWrapper::openStream(const IAudioDriver::StreamConfig& config) {
    if (currentState.load(std::memory_order_acquire) != StreamState::IDLE) {
        return makeErrorResult(StreamError::INVALID_CONFIGURATION, "Stream already open");
    }

    if (!config.client) {
        return makeErrorResult(StreamError::INVALID_CONFIGURATION, "Client callback is null");
    }

    ensureImplInitialized();
    currentConfig = config;
    clientCallback = config.client;

    uint32_t deviceCount = getDeviceCount();
    
    // M-6: Fix off-by-one and validation
    if (config.inputDeviceIndex != UNUSED_DEVICE_INDEX && config.inputDeviceIndex >= deviceCount) {
        return makeErrorResult(StreamError::DEVICE_NOT_FOUND, "Input device out of range");
    }
    if (config.outputDeviceIndex != UNUSED_DEVICE_INDEX && config.outputDeviceIndex >= deviceCount) {
        return makeErrorResult(StreamError::DEVICE_NOT_FOUND, "Output device out of range");
    }

    uint32_t numInputChannels = 0;
    if (config.inputDeviceIndex != UNUSED_DEVICE_INDEX) {
        impl->inputParams = {};
        unsigned int deviceId = impl->deviceIds[config.inputDeviceIndex];
        DeviceInfo info = getDeviceInfo(config.inputDeviceIndex);
        numInputChannels = (config.numInputChannels > 0) ? config.numInputChannels : info.maxInputChannels;
        impl->inputParams.deviceId = deviceId;
        impl->inputParams.nChannels = static_cast<unsigned int>(numInputChannels);
    }

    uint32_t numOutputChannels = 0;
    if (config.outputDeviceIndex != UNUSED_DEVICE_INDEX) {
        impl->outputParams = {};
        unsigned int deviceId = impl->deviceIds[config.outputDeviceIndex];
        DeviceInfo info = getDeviceInfo(config.outputDeviceIndex);
        numOutputChannels = (config.numOutputChannels > 0) ? config.numOutputChannels : info.maxOutputChannels;
        impl->outputParams.deviceId = deviceId;
        impl->outputParams.nChannels = static_cast<unsigned int>(numOutputChannels);
    }

    impl->options = {};
    impl->options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_SCHEDULE_REALTIME;
    impl->options.numberOfBuffers = AudioDefaults::NUM_BUFFERS;
    impl->bufferFrames = config.bufferSize;

    try {
        RtAudioErrorType error = impl->audio->openStream(
            config.outputDeviceIndex != UNUSED_DEVICE_INDEX ? &impl->outputParams : nullptr,
            config.inputDeviceIndex != UNUSED_DEVICE_INDEX ? &impl->inputParams : nullptr,
            RTAUDIO_FLOAT32,
            config.sampleRate,
            &impl->bufferFrames,
            &RtAudioWrapper::rtAudioCallback,
            this,
            &impl->options
        );

        if (error != RTAUDIO_NO_ERROR) {
            std::string errorText = impl->audio->getErrorText();
            closeStream();
            return makeErrorResult(mapRtAudioError(error), errorText.c_str());
        }

        impl->streamOpen = true;
        currentState.store(StreamState::OPEN, std::memory_order_release);
        return OpenResult{.success = true};

    } catch (const std::exception& e) {
        closeStream();
        return makeErrorResult(StreamError::SYSTEM_ERROR, e.what());
    }
}

bool RtAudioWrapper::startStream() {
    if (currentState.load(std::memory_order_acquire) != StreamState::OPEN) return false;
    try {
        if (impl->audio->startStream() != RTAUDIO_NO_ERROR) return false;
        currentState.store(StreamState::RUNNING, std::memory_order_release);
        return true;
    } catch (...) { return false; }
}

bool RtAudioWrapper::stopStream() {
    if (currentState.load(std::memory_order_acquire) != StreamState::RUNNING) return false;
    try {
        if (impl->audio->stopStream() != RTAUDIO_NO_ERROR) return false;
        currentState.store(StreamState::OPEN, std::memory_order_release);
        return true;
    } catch (...) { return false; }
}

void RtAudioWrapper::closeStream() {
    if (currentState.load(std::memory_order_acquire) == StreamState::RUNNING) stopStream();
    if (impl && impl->streamOpen) {
        try { impl->audio->closeStream(); } catch (...) {}
        impl->streamOpen = false;
    }
    currentState.store(StreamState::IDLE, std::memory_order_release);
    clientCallback = nullptr;
    
    // Completely recreate the RtAudio instance to flush any corrupted CoreAudio state
    // caused by problematic devices (like HDMI displays).
    std::lock_guard<std::mutex> lock(implMutex);
    impl.reset();
}

StreamState RtAudioWrapper::getState() const {
    return currentState.load(std::memory_order_acquire);
}

IAudioDriver::StreamConfig RtAudioWrapper::getStreamConfig() const {
    return currentConfig;
}

uint32_t RtAudioWrapper::getDeviceCount() const {
    ensureImplInitialized();
    impl->refreshDeviceIds();
    return static_cast<uint32_t>(impl->deviceIds.size());
}

DeviceInfo RtAudioWrapper::getDeviceInfo(uint32_t deviceIndex) const {
    DeviceInfo info = {};
    ensureImplInitialized();
    if (deviceIndex >= impl->deviceIds.size()) return info;
    
    unsigned int deviceId = impl->deviceIds[deviceIndex];
    try {
        RtAudio::DeviceInfo rtInfo = impl->audio->getDeviceInfo(deviceId);
        size_t nameLen = std::min(rtInfo.name.length(), static_cast<size_t>(MAX_NAME_LENGTH - 1));
        std::memcpy(info.name, rtInfo.name.c_str(), nameLen);
        info.name[nameLen] = '\0';
        std::memcpy(info.manufacturer, info.name, nameLen + 1);
        info.maxInputChannels = static_cast<uint32_t>(rtInfo.inputChannels);
        info.maxOutputChannels = static_cast<uint32_t>(rtInfo.outputChannels);
        info.numSampleRates = 0;
        for (size_t i = 0; i < rtInfo.sampleRates.size() && info.numSampleRates < MAX_SUPPORTED_SAMPLE_RATES; ++i) {
            info.supportedSampleRates[info.numSampleRates++] = static_cast<uint32_t>(rtInfo.sampleRates[i]);
        }
        info.defaultSampleRate = static_cast<uint32_t>(rtInfo.preferredSampleRate);
        info.preferredBufferSize = AudioDefaults::BUFFER_SIZE;
        info.isDefaultInput = rtInfo.isDefaultInput;
        info.isDefaultOutput = rtInfo.isDefaultOutput;
    } catch (...) {}
    return info;
}

int RtAudioWrapper::rtAudioCallback(
    void* outputBuffer,
    void* inputBuffer,
    unsigned int nFrames,
    double,
    unsigned int status,
    void* userData)
{
    static_cast<RtAudioWrapper*>(userData)->processCallback(outputBuffer, inputBuffer, nFrames, status);
    return 0;
}

void RtAudioWrapper::processCallback(
    void* outputBuffer,
    void* inputBuffer,
    unsigned int nFrames,
    unsigned int status)
{
    ScopedDenormalHandler denormalHandler;
    
    if (status != 0 && clientCallback) clientCallback->onXrun();

    uint32_t numInputChannels = (inputBuffer && impl->inputParams.nChannels > 0) ? static_cast<uint32_t>(impl->inputParams.nChannels) : 0;
    uint32_t numOutputChannels = (outputBuffer && impl->outputParams.nChannels > 0) ? static_cast<uint32_t>(impl->outputParams.nChannels) : 0;

    // RtAudio 6.x RTAUDIO_NONINTERLEAVED provides a single buffer with channels concatenated.
    // We must map this to the planar float* const* expected by our client.
    float* inputChannelPtrs[128] = {nullptr}; // Max 128 channels for safety
    float* outputChannelPtrs[128] = {nullptr};

    if (numInputChannels > 0) {
        float* base = static_cast<float*>(inputBuffer);
        uint32_t limit = std::min(numInputChannels, 128u);
        for (uint32_t i = 0; i < limit; ++i) {
            inputChannelPtrs[i] = base + (i * nFrames);
        }
    }

    if (numOutputChannels > 0) {
        float* base = static_cast<float*>(outputBuffer);
        uint32_t limit = std::min(numOutputChannels, 128u);
        for (uint32_t i = 0; i < limit; ++i) {
            outputChannelPtrs[i] = base + (i * nFrames);
        }
    }

    if (!clientCallback) {
        if (numOutputChannels > 0) {
            for (uint32_t i = 0; i < numOutputChannels; ++i) {
                if (outputChannelPtrs[i]) std::memset(outputChannelPtrs[i], 0, nFrames * sizeof(float));
            }
        }
        return;
    }

    // === 3-Phase "Sandwich" Pipeline === //

    // 1. Phase 1: Start Cycle
    clientCallback->startCycle(0, nFrames);

    // 2. Phase 2: Primary Audio Processing
    clientCallback->processAudio(inputChannelPtrs, numInputChannels, outputChannelPtrs, numOutputChannels, nFrames);

    // 3. Phase 3: End Cycle
    clientCallback->endCycle(nFrames);
}


OpenResult RtAudioWrapper::makeErrorResult(StreamError error, const char* message) {
    OpenResult result = {};
    result.success = false;
    result.error = error;
    strncpy(result.errorMessage, message, sizeof(result.errorMessage) - 1);
    result.errorMessage[sizeof(result.errorMessage) - 1] = '\0';
    return result;
}

std::unique_ptr<IAudioDriver> createRtAudioDriver(AudioAPI api) {
    return std::make_unique<RtAudioWrapper>(api);
}

} // namespace Layer1

#endif // HAVE_RTAUDIO
