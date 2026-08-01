// src/Core audio engine/engine/iaudio_engine.h
#pragma once

#include "Hardware/OS abstraction/audio/iaudio_driver.h"
#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core audio engine/transport/itransport.h"
#include "Core infrastructure/clock/iclock_service.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include "Core infrastructure/bridges/itelemetry_bridge.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include "Hardware/OS abstraction/midi/imidi_driver.h"

namespace Layer1 {
class IFileSystem;
}

class IMidiClipDataProvider;

namespace Layer3 {
class IButlerThread;
class IAutomationProcessor;

/**
 * @brief The primary orchestrator for the DAW audio engine.
 * 
 * IAudioEngine implements the 3-Phase "Sandwich" Pipeline:
 * 1. Capture & Sync (Phase 1)
 * 2. DSP Processing (Phase 2)
 * 3. Safety & Telemetry (Phase 3)
 */
class IAudioEngine : public Layer1::IAudioDriver::IAudioClient {
public:
    virtual ~IAudioEngine() = default;

    //==========================================================================
    // Initialization & Wiring
    //==========================================================================

    class IMIDIPlayheadRenderer {
    public:
        virtual ~IMIDIPlayheadRenderer() = default;
        virtual void renderMIDIPlayback(
            uint64_t startSample,
            uint32_t numSamples,
            bool loopEnabled,
            uint64_t loopStart,
            uint64_t loopEnd,
            Layer2::IEventQueue* eventQueue,
            bool isPlaying
        ) = 0;
    };

    virtual void setScheduler(IDSPKernel* scheduler) = 0;
    virtual void setTransport(ITransport* transport) = 0;
    virtual void setClockService(Layer2::IClockService* clock) = 0;
    virtual void setMutationBridge(Layer2::IMutationBridge* mutationBridge) = 0;
    virtual void setEventQueue(Layer2::IEventQueue* eventQueue) = 0;
    virtual void setTelemetryBridge(Layer2::ITelemetryBridge* telemetryBridge) = 0;
    virtual void setTempoService(Layer2::ITempoService* tempoService) = 0;
    virtual void setMIDIDriver(Layer1::IMIDIDriver* midiDriver) = 0;
    virtual void setFileSystem(Layer1::IFileSystem* fileSystem) = 0;
    virtual void setMIDIPlayheadRenderer(IMIDIPlayheadRenderer* renderer) = 0;
    virtual void setMidiClipDataProvider(const IMidiClipDataProvider* provider) = 0;
    virtual void setAutomationProcessor(IAutomationProcessor* processor) = 0;
    virtual void setOfflineExportActive(bool active) = 0;

    //==========================================================================
    // Engine Control
    //==========================================================================

    /**
     * @brief Prepare the engine for a specific hardware configuration.
     * Call before starting the audio stream.
     */
    virtual void prepare(double sampleRate, uint32_t maxBlockSize, Layer1::WorkgroupHandle workgroup = Layer1::WorkgroupHandle::invalid()) = 0;

    /**
     * @brief Reset all internal state and counters.
     */
    virtual void reset() = 0;

    /**
     * @brief Get the current CPU load of the real-time audio thread (0.0 to 1.0)
     */
    virtual double getCpuLoad() const = 0;

    /**
     * @brief Get the total number of audio buffer underruns/overruns (Xruns)
     */
    virtual uint32_t getXrunCount() const = 0;

    virtual TransportState getTransportState() const = 0;
    virtual uint64_t getTransportPosition() const = 0;

    /**
     * @brief Get the total round-trip latency in samples (hardware latency).
     */
    virtual uint32_t getTotalRTLSamples() const = 0;

    /**
     * @brief Get the Plugin Delay Compensation latency in samples for a specific node.
     */
    virtual uint32_t getNodePDCDelaySamples(NodeID nodeId) const = 0;

    /**
     * @brief Get the current sample rate.
     */
    virtual double getSampleRate() const = 0;

    //==========================================================================
    // Butler Thread Access
    //==========================================================================
    virtual IButlerThread* getButlerThread() = 0;

    //==========================================================================
    // Factory
    //==========================================================================

    static std::unique_ptr<IAudioEngine> create();
};

} // namespace Layer3
