// src/Middle Bridge/boot/iboot_controller.h
#pragma once

#include <string>

namespace bridge {

/**
 * @brief Boot stage enumeration representing the current initialization phase
 */
enum class BootStage : uint8_t {
    IDLE = 0,
    INITIALIZING_FILESYSTEM,     // Layer 1: Filesystem & settings load
    INITIALIZING_INFRASTRUCTURE, // Layer 2: Memory registry, bridges, queues
    INITIALIZING_AUDIO,          // Layer 3: Audio engine & hardware stream
    INITIALIZING_MIDI,           // Layer 1/3: MIDI driver and ports
    SCANNING_PLUGINS,            // Layer 3: Loading/scanning VST3, AU, CLAP plugins
    BOOTSTRAPPING_SESSION,       // Layer 5/6: Bootstrapping Composition Root session
    COMPLETED,                   // Boot sequence finished successfully
    FAILED                       // Boot sequence failed with an error
};

/**
 * @brief Interface for the DAW boot sequence controller.
 * Decouples presentation splash screen updates from internal system initialization.
 */
class IBootController {
public:
    virtual ~IBootController() = default;

    /**
     * @brief Listener interface to receive asynchronous boot progress updates.
     * Prevents Qt coupling inside the Middle Bridge.
     */
    class IListener {
    public:
        virtual ~IListener() = default;
        virtual void onBootStageChanged(BootStage stage, const std::string& statusText) = 0;
        virtual void onBootProgressUpdated(float progress) = 0;
        virtual void onBootCompleted() = 0;
        virtual void onBootFailed(const std::string& errorMessage) = 0;
    };

    /**
     * @brief Register a listener for progress callbacks.
     */
    virtual void registerListener(IListener* listener) = 0;

    /**
     * @brief Unregister an existing listener.
     */
    virtual void unregisterListener(IListener* listener) = 0;

    /**
     * @brief Start the boot initialization sequence asynchronously.
     */
    virtual void startBootSequence() = 0;

    /**
     * @brief Cancel/abort an ongoing boot sequence.
     */
    virtual void cancelBootSequence() = 0;

    /**
     * @brief Poll/tick the boot controller state machine.
     * Typically called from the main GUI thread event loop or timer.
     */
    virtual void tick() = 0;

    // === State Queries ===
    virtual BootStage getCurrentStage() const = 0;
    virtual float getProgress() const = 0; // 0.0f to 1.0f
    virtual std::string getStatusText() const = 0;
    virtual std::string getErrorMessage() const = 0;
};

} // namespace bridge
