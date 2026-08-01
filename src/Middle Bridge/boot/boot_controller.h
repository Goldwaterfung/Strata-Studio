// src/Middle Bridge/boot/boot_controller.h
#pragma once

#include "iboot_controller.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <string>

namespace app {
class CompositionRoot;
}

namespace bridge {

/**
 * @brief Concrete BootController that implements the asynchronous boot state machine.
 * Coordinates with app::CompositionRoot to run layer wiring and plugin scanning sequentially.
 */
class BootController : public IBootController {
public:
    explicit BootController(app::CompositionRoot* compositionRoot);
    ~BootController() override;

    // === IBootController Overrides ===
    void registerListener(IListener* listener) override;
    void unregisterListener(IListener* listener) override;
    void startBootSequence() override;
    void cancelBootSequence() override;
    void tick() override;

    BootStage getCurrentStage() const override { return currentStage_.load(); }
    float getProgress() const override { return progress_.load(); }
    std::string getStatusText() const override;
    std::string getErrorMessage() const override;

private:
    void changeStage(BootStage newStage, const std::string& statusText);
    void updateProgress(float progress);
    void handleFailure(const std::string& errorMessage);
    
    // Internal stage execution
    bool executeFSInit();
    bool executeInfraInit();
    bool executeAudioInit();
    bool executeMIDIInit();
    bool executePluginScanTrigger();
    bool pollPluginScan();
    bool executeSessionBootstrap();

    app::CompositionRoot* compositionRoot_;
    
    std::vector<IListener*> listeners_;
    mutable std::mutex listenersMutex_;

    std::atomic<BootStage> currentStage_{BootStage::IDLE};
    std::atomic<float> progress_{0.0f};
    
    std::string statusText_;
    std::string errorMessage_;
    mutable std::mutex textMutex_;
    
    std::atomic<bool> isBooting_{false};
    
    // Polling control for plugin scanning
    int pluginStableCount_ = 0;
    size_t lastPluginCount_ = 0;
    int pluginPollTicks_ = 0;
};

} // namespace bridge
