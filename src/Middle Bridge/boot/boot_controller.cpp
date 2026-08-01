// src/Middle Bridge/boot/boot_controller.cpp
#include "boot_controller.h"
#include "app/composition_root.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include "Hardware/OS abstraction/midi/imidi_driver.h"
#include <iostream>
#include <vector>

namespace bridge {

BootController::BootController(app::CompositionRoot* compositionRoot)
    : compositionRoot_(compositionRoot)
{
}

BootController::~BootController() {
    cancelBootSequence();
}

void BootController::registerListener(IListener* listener) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    if (listener) {
        listeners_.push_back(listener);
    }
}

void BootController::unregisterListener(IListener* listener) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void BootController::startBootSequence() {
    if (isBooting_.load()) return;
    
    isBooting_.store(true);
    changeStage(BootStage::INITIALIZING_FILESYSTEM, "Initializing system directories and loading preferences...");
    updateProgress(0.05f);
}

void BootController::cancelBootSequence() {
    isBooting_.store(false);
    currentStage_.store(BootStage::IDLE);
    progress_.store(0.0f);
}

void BootController::changeStage(BootStage newStage, const std::string& statusText) {
    currentStage_.store(newStage);
    {
        std::lock_guard<std::mutex> lock(textMutex_);
        statusText_ = statusText;
    }

    std::vector<IListener*> listenersCopy;
    {
        std::lock_guard<std::mutex> lock(listenersMutex_);
        listenersCopy = listeners_;
    }

    for (auto* listener : listenersCopy) {
        listener->onBootStageChanged(newStage, statusText);
    }
}

void BootController::updateProgress(float progress) {
    progress_.store(progress);

    std::vector<IListener*> listenersCopy;
    {
        std::lock_guard<std::mutex> lock(listenersMutex_);
        listenersCopy = listeners_;
    }

    for (auto* listener : listenersCopy) {
        listener->onBootProgressUpdated(progress);
    }
}

void BootController::handleFailure(const std::string& errorMessage) {
    isBooting_.store(false);
    currentStage_.store(BootStage::FAILED);
    {
        std::lock_guard<std::mutex> lock(textMutex_);
        errorMessage_ = errorMessage;
        statusText_ = "Initialization Failed: " + errorMessage;
    }

    std::vector<IListener*> listenersCopy;
    {
        std::lock_guard<std::mutex> lock(listenersMutex_);
        listenersCopy = listeners_;
    }

    for (auto* listener : listenersCopy) {
        listener->onBootFailed(errorMessage);
    }
}

std::string BootController::getStatusText() const {
    std::lock_guard<std::mutex> lock(textMutex_);
    return statusText_;
}

std::string BootController::getErrorMessage() const {
    std::lock_guard<std::mutex> lock(textMutex_);
    return errorMessage_;
}

void BootController::tick() {
    if (!isBooting_.load()) return;

    switch (currentStage_.load()) {
        case BootStage::INITIALIZING_FILESYSTEM:
            if (executeFSInit()) {
                changeStage(BootStage::INITIALIZING_INFRASTRUCTURE, "Loading core infrastructure, memory bridges, and event loops...");
                updateProgress(0.15f);
            } else {
                handleFailure("Failed to initialize filesystem Layer 1 HAL.");
            }
            break;

        case BootStage::INITIALIZING_INFRASTRUCTURE:
            if (executeInfraInit()) {
                changeStage(BootStage::INITIALIZING_AUDIO, "Starting core audio engine, routing matrices, and hardware stream...");
                updateProgress(0.30f);
            } else {
                handleFailure("Failed to initialize core infrastructure Layer 2 bridges.");
            }
            break;

        case BootStage::INITIALIZING_AUDIO:
            if (executeAudioInit()) {
                changeStage(BootStage::INITIALIZING_MIDI, "Enumerating MIDI controllers and starting virtual drivers...");
                updateProgress(0.50f);
            } else {
                handleFailure("Failed to establish Core Audio Engine Layer 3/4 stream.");
            }
            break;

        case BootStage::INITIALIZING_MIDI:
            if (executeMIDIInit()) {
                changeStage(BootStage::SCANNING_PLUGINS, "Triggering asynchronous scan for VST3, AU, and CLAP plugins...");
                updateProgress(0.60f);
                pluginPollTicks_ = 0;
            } else {
                handleFailure("Failed to establish Core MIDI Layer 1 HAL.");
            }
            break;

        case BootStage::SCANNING_PLUGINS:
            if (pluginPollTicks_ == 0) {
                if (!executePluginScanTrigger()) {
                    handleFailure("Failed to trigger plugin database scanner.");
                }
            } else {
                if (pollPluginScan()) {
                    changeStage(BootStage::BOOTSTRAPPING_SESSION, "Bootstrapping default project composer session...");
                    updateProgress(0.95f);
                }
            }
            pluginPollTicks_++;
            break;

        case BootStage::BOOTSTRAPPING_SESSION:
            if (executeSessionBootstrap()) {
                isBooting_.store(false);
                changeStage(BootStage::COMPLETED, "System fully initialized. Ready!");
                updateProgress(1.0f);

                // Notify completion
                std::vector<IListener*> listenersCopy;
                {
                    std::lock_guard<std::mutex> lock(listenersMutex_);
                    listenersCopy = listeners_;
                }
                for (auto* listener : listenersCopy) {
                    listener->onBootCompleted();
                }
            } else {
                handleFailure("Failed to bootstrap the initial project session.");
            }
            break;

        default:
            break;
    }
}

// === Individual step implementations calling CompositionRoot private friends ===

bool BootController::executeFSInit() {
    if (!compositionRoot_) return false;
    return compositionRoot_->wireLayer1ToLayer2();
}

bool BootController::executeInfraInit() {
    if (!compositionRoot_) return false;
    return compositionRoot_->wireLayer2ToLayer3();
}

bool BootController::executeAudioInit() {
    if (!compositionRoot_) return false;
    
    // Wire Layers 3 through 7 sequentially (fast operations)
    if (!compositionRoot_->wireLayer3ToLayer4()) return false;
    if (!compositionRoot_->wireLayer4ToLayer5()) return false;
    if (!compositionRoot_->wireLayer5ToLayer6()) return false;
    if (!compositionRoot_->wireLayer6ToLayer7()) return false;

    // Connect subsystems & open audio HAL stream
    if (!compositionRoot_->connectAudioToBridges()) return false;
    if (!compositionRoot_->connectTransportToScheduler()) return false;
    if (!compositionRoot_->connectDSPToComposition()) return false;
    if (!compositionRoot_->connectMediaToPresentation()) return false;

    return true;
}

bool BootController::executeMIDIInit() {
    auto midiDriver = Layer1::IMIDIDriver::create(Layer1::AudioAPI::CORE_AUDIO);
    return midiDriver != nullptr;
}

bool BootController::executePluginScanTrigger() {
    if (!compositionRoot_ || !compositionRoot_->pluginManager_) return false;
    
    auto paths = app::CompositionRoot::getDefaultPluginScanDirectories();
    compositionRoot_->pluginManager_->scanForPlugins(paths);
    return true;
}

bool BootController::pollPluginScan() {
    if (!compositionRoot_ || !compositionRoot_->pluginManager_) return true;

    auto* pm = compositionRoot_->pluginManager_.get();
    bool scanning = pm->isScanning();
    float scanProgress = pm->getScanProgress();
    std::string currentPlugName = pm->getCurrentlyScanningPlugin();

    // Check if stable or done
    if (!scanning) {
        return true;
    }

    // Dynamic progress interpolation (between 0.60 and 0.90)
    float baseProgress = 0.60f;
    float scale = 0.30f;
    float currentProgress = baseProgress + (scanProgress * scale);
    updateProgress(currentProgress);

    if (!currentPlugName.empty()) {
        std::lock_guard<std::mutex> lock(textMutex_);
        statusText_ = "Scanning Plugins: " + currentPlugName;
    } else {
        std::lock_guard<std::mutex> lock(textMutex_);
        statusText_ = "Scanning third-party plugin database...";
    }

    // Notify listeners of progress change inside tick
    std::vector<IListener*> listenersCopy;
    {
        std::lock_guard<std::mutex> lock(listenersMutex_);
        listenersCopy = listeners_;
    }
    for (auto* listener : listenersCopy) {
        listener->onBootStageChanged(BootStage::SCANNING_PLUGINS, statusText_);
    }

    return false;
}

bool BootController::executeSessionBootstrap() {
    // Bootstrap session in CompositionRoot
    // (This matches the session setup at the end of wireLayer6ToLayer7)
    return true;
}

} // namespace bridge
