// asio_audio_driver.cpp
// Layer 1: Hardware/OS Abstraction - Windows ASIO Audio Driver Implementation

#ifdef _WIN32

#include "asio_audio_driver.h"
#include <windows.h>
#include <vector>
#include <atomic>
#include <algorithm>
#include <string>

// ASIO SDK Headers (Assuming they are in include path)
#include "asio.h"

namespace Layer1 {

// Registry Helper for ASIO Driver Enumeration
struct ASIODriverInfo {
    std::string name;
    CLSID clsid;
};

static std::vector<ASIODriverInfo> enumerateASIODrivers() {
    std::vector<ASIODriverInfo> drivers;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\ASIO", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return drivers;
    }

    char subKeyName[256];
    DWORD index = 0;
    while (RegEnumKeyA(hKey, index++, subKeyName, sizeof(subKeyName)) == ERROR_SUCCESS) {
        HKEY hSubKey;
        if (RegOpenKeyExA(hKey, subKeyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
            char clsidStr[128];
            DWORD size = sizeof(clsidStr);
            if (RegQueryValueExA(hSubKey, "CLSID", nullptr, nullptr, (LPBYTE)clsidStr, &size) == ERROR_SUCCESS) {
                ASIODriverInfo info;
                info.name = subKeyName;
                
                // Convert string CLSID to CLSID struct
                std::wstring wclsid = std::wstring(clsidStr, clsidStr + strlen(clsidStr));
                if (CLSIDFromString(wclsid.c_str(), &info.clsid) == S_OK) {
                    drivers.push_back(info);
                }
            }
            RegCloseKey(hSubKey);
        }
    }
    RegCloseKey(hKey);
    return drivers;
}

// =============================================================================
// INTERNAL IMPLEMENTATION
// =============================================================================

class ASIOAudioDriver::Impl {
public:
    Impl() : state(StreamState::IDLE), asioStarted(false) {}
    
    ~Impl() {
        cleanup();
    }

    std::atomic<StreamState> state;
    IAudioDriver::StreamConfig config;
    bool asioStarted;

    // ASIO Driver Properties
    long numInputChannels = 0;
    long numOutputChannels = 0;
    long minSize = 0, maxSize = 0, preferredSize = 0, granularity = 0;
    ASIOSampleRate sampleRate = 0;

    // Buffers
    std::vector<ASIOBufferInfo> bufferInfos;
    std::vector<ASIOChannelInfo> channelInfos;
    
    // Internal Planar Buffers for Layer 3
    std::vector<float*> inputPlanarBuffers;
    std::vector<float*> outputPlanarBuffers;
    std::vector<std::vector<float>> inputData;
    std::vector<std::vector<float>> outputData;

    static Impl* currentInstance;

    void cleanup() {
        if (asioStarted) {
            ASIOStop();
            asioStarted = false;
        }
        ASIODisposeBuffers();
        ASIOExit();
        state = StreamState::IDLE;
    }

    // ASIO Callbacks
    static ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess) {
        if (currentInstance) {
            currentInstance->handleBufferSwitch(doubleBufferIndex);
        }
        return params;
    }

    static void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
        // Fallback if the driver doesn't support time info
        if (currentInstance) {
            currentInstance->handleBufferSwitch(doubleBufferIndex);
        }
    }

    static void sampleRateChanged(ASIOSampleRate sRate) {
        if (currentInstance && currentInstance->config.client) {
            currentInstance->config.client->onSampleRateChanged(static_cast<uint32_t>(sRate));
        }
    }

    static long asioMessages(long selector, long value, void* message, double* opt) {
        // Handle important ASIO messages (e.g. kAsioEngineVersion, kAsioResetRequest)
        if (selector == kAsioResetRequest) {
            // Re-open driver requested
            return 1;
        }
        return 0;
    }

    void handleBufferSwitch(long index) {
        // 1. Phase 1: Start Cycle & Input Capture
        if (config.client) {
            // Using QueryPerformanceCounter for high-res timestamp on Windows
            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            config.client->startCycle(qpc.QuadPart, config.bufferSize);
        }

        for (uint32_t i = 0; i < config.numInputChannels; ++i) {
            void* src = bufferInfos[i].buffers[index];
            float* dest = inputData[i].data();
            convertFromASIO(src, dest, config.bufferSize, channelInfos[i].type);
        }

        // 2. Phase 2: Processing
        for (uint32_t i = 0; i < config.numOutputChannels; ++i) {
            std::fill(outputData[i].begin(), outputData[i].end(), 0.0f);
        }

        if (config.client) {
            config.client->processAudio(
                inputPlanarBuffers.data(), config.numInputChannels,
                outputPlanarBuffers.data(), config.numOutputChannels,
                config.bufferSize
            );
        }

        // 3. Phase 3: End Cycle & Output Sync
        if (config.client) {
            config.client->endCycle(config.bufferSize);
        }

        for (uint32_t i = 0; i < config.numOutputChannels; ++i) {
            float* src = outputData[i].data();
            void* dest = bufferInfos[config.numInputChannels + i].buffers[index];
            convertToASIO(src, dest, config.bufferSize, channelInfos[config.numInputChannels + i].type);
        }
    }

    // format converters (simplified for Float32 and Int32)
    void convertFromASIO(void* src, float* dest, uint32_t frames, ASIOSampleType type) {
        switch (type) {
            case ASIOSTFloat32LSB:
                std::memcpy(dest, src, frames * sizeof(float));
                break;
            case ASIOSTInt32LSB: {
                int32_t* s = static_cast<int32_t*>(src);
                for (uint32_t i = 0; i < frames; ++i) dest[i] = s[i] / 2147483648.0f;
                break;
            }
            case ASIOSTInt24LSB: {
                uint8_t* s = static_cast<uint8_t*>(src);
                for (uint32_t i = 0; i < frames; ++i) {
                    int32_t val = (s[i*3] << 8) | (s[i*3+1] << 16) | (s[i*3+2] << 24);
                    dest[i] = val / 2147483648.0f;
                }
                break;
            }
            case ASIOSTInt16LSB: {
                int16_t* s = static_cast<int16_t*>(src);
                for (uint32_t i = 0; i < frames; ++i) dest[i] = s[i] / 32768.0f;
                break;
            }
            default:
                std::memset(dest, 0, frames * sizeof(float));
                break;
        }
    }

    void convertToASIO(float* src, void* dest, uint32_t frames, ASIOSampleType type) {
        switch (type) {
            case ASIOSTFloat32LSB:
                std::memcpy(dest, src, frames * sizeof(float));
                break;
            case ASIOSTInt32LSB: {
                int32_t* d = static_cast<int32_t*>(dest);
                for (uint32_t i = 0; i < frames; ++i) d[i] = static_cast<int32_t>(src[i] * 2147483647.0f);
                break;
            }
            case ASIOSTInt24LSB: {
                uint8_t* d = static_cast<uint8_t*>(dest);
                for (uint32_t i = 0; i < frames; ++i) {
                    int32_t val = static_cast<int32_t>(src[i] * 2147483647.0f);
                    d[i*3] = (val >> 8) & 0xFF;
                    d[i*3+1] = (val >> 16) & 0xFF;
                    d[i*3+2] = (val >> 24) & 0xFF;
                }
                break;
            }
            case ASIOSTInt16LSB: {
                int16_t* d = static_cast<int16_t*>(dest);
                for (uint32_t i = 0; i < frames; ++i) d[i] = static_cast<int16_t>(src[i] * 32767.0f);
                break;
            }
            default:
                break;
        }
    }
};

ASIOAudioDriver::Impl* ASIOAudioDriver::Impl::currentInstance = nullptr;

// =============================================================================
// PUBLIC WRAPPERS
// =============================================================================

ASIOAudioDriver::ASIOAudioDriver() : pImpl(std::make_unique<Impl>()) {}
ASIOAudioDriver::~ASIOAudioDriver() = default;

OpenResult ASIOAudioDriver::openStream(const StreamConfig& config) {
    auto drivers = enumerateASIODrivers();
    if (config.outputDeviceIndex >= drivers.size()) {
        return {false, StreamError::DEVICE_NOT_FOUND, "Invalid ASIO device index"};
    }

    // Load Driver (In a real app, you'd use ASIOInit with the CLSID)
    // For this implementation, we assume the ASIO SDK's host code handles the loading
    // but we'll manually call ASIOInit.
    if (ASIOInit(const_cast<ASIODriverInfo*>(&drivers[config.outputDeviceIndex])->clsid != S_OK)) {
         return {false, StreamError::SYSTEM_ERROR, "ASIOInit failed"};
    }

    pImpl->config = config;
    Impl::currentInstance = pImpl.get();

    ASIOGetChannels(&pImpl->numInputChannels, &pImpl->numOutputChannels);
    ASIOGetBufferSize(&pImpl->minSize, &pImpl->maxSize, &pImpl->preferredSize, &pImpl->granularity);
    ASIOGetSampleRate(&pImpl->sampleRate);

    // Create Buffers
    long totalChannels = config.numInputChannels + config.numOutputChannels;
    pImpl->bufferInfos.resize(totalChannels);
    pImpl->channelInfos.resize(totalChannels);

    for (uint32_t i = 0; i < config.numInputChannels; ++i) {
        pImpl->bufferInfos[i].isInput = ASIOTrue;
        pImpl->bufferInfos[i].channelNum = i;
    }
    for (uint32_t i = 0; i < config.numOutputChannels; ++i) {
        pImpl->bufferInfos[config.numInputChannels + i].isInput = ASIOFalse;
        pImpl->bufferInfos[config.numInputChannels + i].channelNum = i;
    }

    static ASIOCallbacks callbacks;
    callbacks.bufferSwitch = Impl::bufferSwitch;
    callbacks.sampleRateDidChange = Impl::sampleRateChanged;
    callbacks.asioMessage = Impl::asioMessages;
    callbacks.bufferSwitchTimeInfo = Impl::bufferSwitchTimeInfo;

    if (ASIOCreateBuffers(pImpl->bufferInfos.data(), totalChannels, config.bufferSize, &callbacks) != ASE_OK) {
        return {false, StreamError::SYSTEM_ERROR, "ASIOCreateBuffers failed"};
    }

    // Initialize Planar Buffers
    pImpl->inputData.resize(config.numInputChannels, std::vector<float>(config.bufferSize));
    pImpl->outputData.resize(config.numOutputChannels, std::vector<float>(config.bufferSize));
    pImpl->inputPlanarBuffers.resize(config.numInputChannels);
    pImpl->outputPlanarBuffers.resize(config.numOutputChannels);
    for(uint32_t i=0; i<config.numInputChannels; ++i) pImpl->inputPlanarBuffers[i] = pImpl->inputData[i].data();
    for(uint32_t i=0; i<config.numOutputChannels; ++i) pImpl->outputPlanarBuffers[i] = pImpl->outputData[i].data();

    pImpl->state = StreamState::OPEN;
    return {true, StreamError::NONE, ""};
}

bool ASIOAudioDriver::startStream() {
    if (pImpl->state != StreamState::OPEN) return false;
    if (ASIOStart() != ASE_OK) return false;
    pImpl->asioStarted = true;
    pImpl->state = StreamState::RUNNING;
    return true;
}

bool ASIOAudioDriver::stopStream() {
    if (pImpl->state != StreamState::RUNNING) return false;
    ASIOStop();
    pImpl->asioStarted = false;
    pImpl->state = StreamState::OPEN;
    return true;
}

void ASIOAudioDriver::closeStream() {
    pImpl->cleanup();
}

StreamState ASIOAudioDriver::getState() const {
    return pImpl->state;
}

uint32_t ASIOAudioDriver::getDeviceCount() const {
    return (uint32_t)enumerateASIODrivers().size();
}

DeviceInfo ASIOAudioDriver::getDeviceInfo(uint32_t deviceIndex) const {
    auto drivers = enumerateASIODrivers();
    DeviceInfo info = {};
    if (deviceIndex < drivers.size()) {
        std::strncpy(info.name, drivers[deviceIndex].name.c_str(), sizeof(info.name)-1);
    }
    return info;
}

} // namespace Layer1

#endif
