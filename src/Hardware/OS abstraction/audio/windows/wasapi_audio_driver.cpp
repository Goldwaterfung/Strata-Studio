// wasapi_audio_driver.cpp
// Layer 1: Hardware/OS Abstraction - Windows WASAPI Audio Driver Implementation

#ifdef _WIN32

#include "wasapi_audio_driver.h"
#include "../audio_format_converter.h"
#include "../audio_utils.h"
#include "../audio_driver_defaults.h"

// Platform headers (QUARANTINED to this file)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <iostream>
#include <cstring>
#include <atomic>
#include <vector>
#include <algorithm>

namespace Layer1 {

// =============================================================================
// INTERNAL IMPLEMENTATION (PIMPL)
// =============================================================================

struct DeviceState {
    IMMDevice* device = nullptr;
    ::IAudioClient* audioClient = nullptr; // Explicitly use global namespace for Windows COM interface
    IAudioRenderClient* renderClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
    UINT32 bufferFrameCount = 0;
    WAVEFORMATEX* mixFormat = nullptr;
    HANDLE eventHandle = nullptr;
};

class WASAPIAudioDriver::Impl {
public:
    Impl()
        : currentState(StreamState::IDLE)
        , devicesCached(false)
        , deviceEnumerator(nullptr)
        , callbackThread(nullptr)
        , threadRunning(false)
    {
        initializeCOM();
    }

    ~Impl() {
        cleanupCOM();
    }

    // === Members (moved from WASAPIAudioDriver) ===

    std::atomic<StreamState> currentState;
    IAudioDriver::StreamConfig currentConfig;

    std::vector<DeviceInfo> cachedDevices;
    std::atomic<bool> devicesCached;

    IMMDeviceEnumerator* deviceEnumerator;
    DeviceState inputState;
    DeviceState outputState;

    HANDLE callbackThread;
    std::atomic<bool> threadRunning;

    std::vector<float*> inputPlanarBuffers;
    std::vector<std::vector<float>> inputPlanarData;
    std::vector<float*> outputPlanarBuffers;
    std::vector<std::vector<float>> outputPlanarData;

    // === Logic Methods ===

    HRESULT initializeCOM() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == RPC_E_CHANGED_MODE) return S_OK;
        return hr;
    }

    void cleanupCOM() {
        if (deviceEnumerator) {
            deviceEnumerator->Release();
            deviceEnumerator = nullptr;
        }
        CoUninitialize();
    }

    HRESULT enumerateDevices() {
        HRESULT hr;
        if (!deviceEnumerator) {
            hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                 CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                 reinterpret_cast<void**>(&deviceEnumerator));
            if (FAILED(hr)) return hr;
        }

        IMMDeviceCollection* deviceCollection = nullptr;
        hr = deviceEnumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &deviceCollection);
        if (FAILED(hr)) return hr;

        UINT32 count = 0;
        hr = deviceCollection->GetCount(&count);
        if (FAILED(hr)) {
            deviceCollection->Release();
            return hr;
        }

        cachedDevices.clear();
        cachedDevices.reserve(count);

        for (UINT32 i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            hr = deviceCollection->Item(i, &device);
            if (SUCCEEDED(hr)) {
                cachedDevices.push_back(createDeviceInfo(device));
                device->Release();
            }
        }

        deviceCollection->Release();
        devicesCached.store(true);
        return S_OK;
    }

    DeviceInfo createDeviceInfo(IMMDevice* device) {
        DeviceInfo info = {};
        LPWSTR deviceId = nullptr;
        if (SUCCEEDED(device->GetId(&deviceId)) && deviceId) {
            WideCharToMultiByte(CP_UTF8, 0, deviceId, -1, info.name, MAX_NAME_LENGTH, nullptr, nullptr);
            CoTaskMemFree(deviceId);
        }

        IPropertyStore* props = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName)) && varName.pwszVal) {
                WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, info.name, MAX_NAME_LENGTH, nullptr, nullptr);
                PropVariantClear(&varName);
            }
            props->Release();
        }

        ::IAudioClient* audioClient = nullptr;
        if (SUCCEEDED(device->Activate(__uuidof(::IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audioClient)))) {
            WAVEFORMATEX* format = nullptr;
            if (SUCCEEDED(audioClient->GetMixFormat(&format)) && format) {
                info.maxInputChannels = format->nChannels;
                info.maxOutputChannels = format->nChannels;
                info.defaultSampleRate = format->nSamplesPerSec;

                info.numSampleRates = 0;
                for (uint32_t rate : AudioDefaults::COMMON_SAMPLE_RATES) {
                    if (info.numSampleRates < MAX_SUPPORTED_SAMPLE_RATES) {
                        info.supportedSampleRates[info.numSampleRates++] = rate;
                    }
                }
                info.preferredBufferSize = AudioDefaults::BUFFER_SIZE;
                CoTaskMemFree(format);
            }
            audioClient->Release();
        }
        return info;
    }

    HRESULT openDevice(UINT32 deviceIndex, bool forInput, DeviceState& state) {
        HRESULT hr;
        if (!deviceEnumerator) enumerateDevices();

        IMMDeviceCollection* deviceCollection = nullptr;
        hr = deviceEnumerator->EnumAudioEndpoints(forInput ? eCapture : eRender, DEVICE_STATE_ACTIVE, &deviceCollection);
        if (FAILED(hr)) return hr;

        IMMDevice* device = nullptr;
        hr = deviceCollection->Item(deviceIndex, &device);
        deviceCollection->Release();
        if (FAILED(hr)) return hr;

        state.device = device;
        hr = device->Activate(__uuidof(::IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&state.audioClient));
        if (FAILED(hr)) return hr;

        hr = state.audioClient->GetMixFormat(&state.mixFormat);
        if (FAILED(hr)) return hr;

        hr = state.audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, state.mixFormat, nullptr);
        if (FAILED(hr)) return hr;

        state.eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!state.eventHandle) return E_FAIL;

        hr = state.audioClient->SetEventHandle(state.eventHandle);
        if (FAILED(hr)) return hr;

        hr = state.audioClient->GetBufferSize(&state.bufferFrameCount);
        if (FAILED(hr)) return hr;

        if (forInput) {
            hr = state.audioClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&state.captureClient));
        } else {
            hr = state.audioClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&state.renderClient));
        }
        return hr;
    }

    void closeDevice(DeviceState& state) {
        if (state.mixFormat) { CoTaskMemFree(state.mixFormat); state.mixFormat = nullptr; }
        if (state.renderClient) { state.renderClient->Release(); state.renderClient = nullptr; }
        if (state.captureClient) { state.captureClient->Release(); state.captureClient = nullptr; }
        if (state.audioClient) { state.audioClient->Release(); state.audioClient = nullptr; }
        if (state.device) { state.device->Release(); state.device = nullptr; }
        if (state.eventHandle) { CloseHandle(state.eventHandle); state.eventHandle = nullptr; }
        state.bufferFrameCount = 0;
    }

    static DWORD WINAPI wasapiThreadEntryPoint(LPVOID lpParam) {
        Impl* impl = static_cast<Impl*>(lpParam);
        impl->processAudioLoop();
        return 0;
    }

    void processAudioLoop() {
        ScopedDenormalHandler denormalHandler;
        HANDLE waitHandles[2];
        DWORD numHandles = 0;

        if (outputState.eventHandle) {
            waitHandles[numHandles++] = outputState.eventHandle;
        } else if (inputState.eventHandle) {
            waitHandles[numHandles++] = inputState.eventHandle;
        }

        if (numHandles == 0) return;

        while (threadRunning.load(std::memory_order_relaxed)) {
            DWORD waitResult = WaitForMultipleObjects(numHandles, waitHandles, FALSE, AudioDefaults::WAIT_TIMEOUT_MS);
            if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + numHandles) {
                processAudioCallback();
            } else if (waitResult == WAIT_TIMEOUT) {
                continue;
            } else {
                break;
            }
        }
    }

    void processAudioCallback() {
        UINT32 numFrames = currentConfig.bufferSize;
        
        // 1. Phase 1: Start Cycle & Input Capture
        if (currentConfig.client) {
            // Using QueryPerformanceCounter for high-res timestamp
            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            currentConfig.client->startCycle(qpc.QuadPart, numFrames);
        }

        float* const* inputChannels = nullptr;
        if (inputState.captureClient) {
            UINT32 packetSize = 0;
            HRESULT hr = inputState.captureClient->GetNextPacketSize(&packetSize);
            
            if (SUCCEEDED(hr) && packetSize > 0) {
                BYTE* pInputData = nullptr;
                UINT32 numFramesRead = 0;
                DWORD flags = 0;
                
                hr = inputState.captureClient->GetBuffer(&pInputData, &numFramesRead, &flags, nullptr, nullptr);
                if (SUCCEEDED(hr)) {
                    // Check for capture overflow (Xrun)
                    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                        if (currentConfig.client) currentConfig.client->onXrun();
                    }

                    // Convert interleaved to planar
                    uint32_t numInputChannels = inputState.mixFormat->nChannels;
                    
                    // If buffer is silent, clear it
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        for (uint32_t ch = 0; ch < numInputChannels; ++ch) {
                            std::fill(inputPlanarData[ch].begin(), inputPlanarData[ch].end(), 0.0f);
                        }
                    } else {
                        AudioFormatConverter::interleavedToPlanar_Generic(
                            reinterpret_cast<const float*>(pInputData),
                            inputPlanarBuffers.data(),
                            numInputChannels,
                            std::min(numFramesRead, numFrames)
                        );
                    }
                    
                    inputChannels = inputPlanarBuffers.data();
                    inputState.captureClient->ReleaseBuffer(numFramesRead);
                }
            }
        }

        // 2. Phase 2: Processing
        if (outputState.renderClient) {
            BYTE* pOutputData = nullptr;
            HRESULT hr = outputState.renderClient->GetBuffer(numFrames, &pOutputData);

            if (SUCCEEDED(hr)) {
                // Clear output buffers
                for (auto& ch : outputPlanarData) std::fill(ch.begin(), ch.end(), 0.0f);

                // Invoke client callback
                if (currentConfig.client) {
                    currentConfig.client->processAudio(
                        inputChannels,
                        static_cast<uint32_t>(inputPlanarBuffers.size()),
                        outputPlanarBuffers.data(),
                        static_cast<uint32_t>(outputPlanarBuffers.size()),
                        numFrames
                    );
                }

                // Convert planar to interleaved
                AudioFormatConverter::planarToInterleaved_Generic(
                    outputPlanarBuffers.data(),
                    reinterpret_cast<float*>(pOutputData),
                    outputState.mixFormat->nChannels,
                    numFrames
                );

                outputState.renderClient->ReleaseBuffer(numFrames, 0);
            } else if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                threadRunning = false;
                currentState = StreamState::DISCONNECTED;
                if (currentConfig.client) currentConfig.client->onDeviceDisconnected();
            } else if (hr == AUDCLNT_E_BUFFER_SIZE_ERROR || hr == AUDCLNT_E_OUT_OF_ORDER) {
                if (currentConfig.client) currentConfig.client->onXrun();
            }
        }

        // 3. Phase 3: End Cycle
        if (currentConfig.client) {
            currentConfig.client->endCycle(numFrames);
        }

    OpenResult makeErrorResult(StreamError error, const char* message) {
        OpenResult result;
        result.success = false;
        result.error = error;
        strncpy(result.errorMessage, message, sizeof(result.errorMessage) - 1);
        result.errorMessage[sizeof(result.errorMessage) - 1] = '\0';
        return result;
    }

    bool validateStateTransition(StreamState targetState) {
        switch (targetState) {
            case StreamState::OPEN:
                return currentState == StreamState::IDLE || currentState == StreamState::RUNNING;
            case StreamState::RUNNING:
                return currentState == StreamState::OPEN;
            case StreamState::IDLE:
                return true;
            case StreamState::DISCONNECTED:
                return true;
            default:
                return false;
        }
    }
};

// =============================================================================
// WASAPIAudioDriver Wrapper Methods
// =============================================================================

WASAPIAudioDriver::WASAPIAudioDriver() : pImpl(std::make_unique<Impl>()) {}
WASAPIAudioDriver::~WASAPIAudioDriver() = default;

OpenResult WASAPIAudioDriver::openStream(const IAudioDriver::StreamConfig& config) {
    if (!pImpl->validateStateTransition(StreamState::OPEN)) {
        return pImpl->makeErrorResult(StreamError::INVALID_CONFIGURATION, "Stream already open or in invalid state");
    }

    if (!config.client) {
        return pImpl->makeErrorResult(StreamError::INVALID_CONFIGURATION, "Client callback is null");
    }

    if (!pImpl->devicesCached.load()) {
        if (FAILED(pImpl->enumerateDevices())) {
            return pImpl->makeErrorResult(StreamError::SYSTEM_ERROR, "Failed to enumerate audio devices");
        }
    }

    // Validate device indices
    if (config.inputDeviceIndex >= pImpl->cachedDevices.size() && config.inputDeviceIndex != UNUSED_DEVICE_INDEX) {
        return pImpl->makeErrorResult(StreamError::DEVICE_NOT_FOUND, "Input device index out of range");
    }
    if (config.outputDeviceIndex >= pImpl->cachedDevices.size() && config.outputDeviceIndex != UNUSED_DEVICE_INDEX) {
        return pImpl->makeErrorResult(StreamError::DEVICE_NOT_FOUND, "Output device index out of range");
    }

    pImpl->currentConfig = config;

    if (config.inputDeviceIndex != UNUSED_DEVICE_INDEX) {
        if (FAILED(pImpl->openDevice(config.inputDeviceIndex, true, pImpl->inputState))) {
            closeStream();
            return pImpl->makeErrorResult(StreamError::DEVICE_IN_USE, "Failed to open input device");
        }
        
        uint32_t numChannels = (config.numInputChannels > 0) ? config.numInputChannels : pImpl->inputState.mixFormat->nChannels;
        // Clamp to device max
        numChannels = std::min(numChannels, static_cast<uint32_t>(pImpl->inputState.mixFormat->nChannels));
        
        pImpl->inputPlanarData.resize(numChannels);
        pImpl->inputPlanarBuffers.resize(numChannels);
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            pImpl->inputPlanarData[ch].resize(config.bufferSize);
            pImpl->inputPlanarBuffers[ch] = pImpl->inputPlanarData[ch].data();
        }
    }

    if (config.outputDeviceIndex != UNUSED_DEVICE_INDEX) {
        if (FAILED(pImpl->openDevice(config.outputDeviceIndex, false, pImpl->outputState))) {
            closeStream();
            return pImpl->makeErrorResult(StreamError::DEVICE_IN_USE, "Failed to open output device");
        }

        uint32_t numChannels = (config.numOutputChannels > 0) ? config.numOutputChannels : pImpl->outputState.mixFormat->nChannels;
        // Clamp to device max
        numChannels = std::min(numChannels, static_cast<uint32_t>(pImpl->outputState.mixFormat->nChannels));

        pImpl->outputPlanarData.resize(numChannels);
        pImpl->outputPlanarBuffers.resize(numChannels);
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            pImpl->outputPlanarData[ch].resize(config.bufferSize);
            pImpl->outputPlanarBuffers[ch] = pImpl->outputPlanarData[ch].data();
        }
    }

    pImpl->currentState = StreamState::OPEN;
    return OpenResult{.success = true};
}

bool WASAPIAudioDriver::startStream() {
    if (!pImpl->validateStateTransition(StreamState::RUNNING)) return false;

    pImpl->threadRunning = true;
    pImpl->callbackThread = CreateThread(nullptr, 0, Impl::wasapiThreadEntryPoint, pImpl.get(), 0, nullptr);
    if (!pImpl->callbackThread) {
        pImpl->threadRunning = false;
        return false;
    }

    // Set real-time priority for the audio thread
    SetThreadPriority(pImpl->callbackThread, THREAD_PRIORITY_TIME_CRITICAL);

    if (pImpl->inputState.audioClient) {
        if (FAILED(pImpl->inputState.audioClient->Start())) {
            pImpl->threadRunning = false;
            WaitForSingleObject(pImpl->callbackThread, INFINITE);
            CloseHandle(pImpl->callbackThread);
            pImpl->callbackThread = nullptr;
            return false;
        }
    }

    if (pImpl->outputState.audioClient) {
        if (FAILED(pImpl->outputState.audioClient->Start())) {
            if (pImpl->inputState.audioClient) pImpl->inputState.audioClient->Stop();
            pImpl->threadRunning = false;
            WaitForSingleObject(pImpl->callbackThread, INFINITE);
            CloseHandle(pImpl->callbackThread);
            pImpl->callbackThread = nullptr;
            return false;
        }
    }

    pImpl->currentState = StreamState::RUNNING;
    return true;
}

bool WASAPIAudioDriver::stopStream() {
    if (!pImpl->validateStateTransition(StreamState::OPEN)) return false;

    pImpl->threadRunning = false;
    if (pImpl->callbackThread) {
        if (pImpl->outputState.eventHandle) SetEvent(pImpl->outputState.eventHandle);
        if (pImpl->inputState.eventHandle) SetEvent(pImpl->inputState.eventHandle);
        WaitForSingleObject(pImpl->callbackThread, INFINITE);
        CloseHandle(pImpl->callbackThread);
        pImpl->callbackThread = nullptr;
    }

    if (pImpl->inputState.audioClient) pImpl->inputState.audioClient->Stop();
    if (pImpl->outputState.audioClient) pImpl->outputState.audioClient->Stop();

    pImpl->currentState = StreamState::OPEN;
    return true;
}

void WASAPIAudioDriver::closeStream() {
    if (pImpl->currentState == StreamState::RUNNING) stopStream();
    pImpl->closeDevice(pImpl->inputState);
    pImpl->closeDevice(pImpl->outputState);
    pImpl->inputPlanarData.clear();
    pImpl->inputPlanarBuffers.clear();
    pImpl->outputPlanarData.clear();
    pImpl->outputPlanarBuffers.clear();
    pImpl->currentState = StreamState::IDLE;
}

StreamState WASAPIAudioDriver::getState() const {
    return pImpl->currentState;
}

uint32_t WASAPIAudioDriver::getDeviceCount() const {
    if (!pImpl->devicesCached.load()) {
        const_cast<Impl*>(pImpl.get())->enumerateDevices();
    }
    return static_cast<uint32_t>(pImpl->cachedDevices.size());
}

DeviceInfo WASAPIAudioDriver::getDeviceInfo(uint32_t deviceIndex) const {
    if (deviceIndex >= pImpl->cachedDevices.size()) return DeviceInfo{};
    return pImpl->cachedDevices[deviceIndex];
}

} // namespace Layer1

#endif // _WIN32
