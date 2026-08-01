// src/Core audio engine/engine/audio_engine_impl.h
#pragma once

#include "Core audio engine/engine/iaudio_engine.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include "../scheduler/offline_dsp_thread_pool.h"
#include "../streaming/pre_baker.h"
#include <atomic>

class IMidiClipDataProvider;

namespace Layer3 {

class AudioEngineImpl : public IAudioEngine {
public:
    AudioEngineImpl();
    ~AudioEngineImpl() override;

    // IAudioEngine implementation
    void setScheduler(IDSPKernel* scheduler) override { scheduler_ = scheduler; }
    void setTransport(ITransport* transport) override { transport_ = transport; }
    void setClockService(Layer2::IClockService* clock) override { clock_ = clock; }
    void setMutationBridge(Layer2::IMutationBridge* bridge) override { mutationBridge_ = bridge; }
    void setEventQueue(Layer2::IEventQueue* queue) override { eventQueue_ = queue; }
    void setTelemetryBridge(Layer2::ITelemetryBridge* bridge) override { telemetryBridge_ = bridge; }
    void setTempoService(Layer2::ITempoService* service) override { tempoService_ = service; }
    void setMIDIDriver(Layer1::IMIDIDriver* driver) override { midiDriver_ = driver; }
    void setFileSystem(Layer1::IFileSystem* fs) override { 
        if (butlerThread_) butlerThread_->attachFileSystem(fs); 
    }
    void setMIDIPlayheadRenderer(IMIDIPlayheadRenderer* renderer) override {
        midiPlayheadRenderer_ = renderer;
    }
    void setMidiClipDataProvider(const IMidiClipDataProvider* provider) override;
    void setAutomationProcessor(IAutomationProcessor* processor) override;
    void setOfflineExportActive(bool active) override;

    void prepare(double sampleRate, uint32_t maxBlockSize, Layer1::WorkgroupHandle workgroup = Layer1::WorkgroupHandle::invalid()) override;
    void reset() override;

    double getCpuLoad() const override { return cpuLoad_.load(std::memory_order_relaxed); }
    uint32_t getXrunCount() const override { return xrunCount_.load(std::memory_order_relaxed); }

    TransportState getTransportState() const override;
    uint64_t getTransportPosition() const override;

    // Latency Compensation (delegates to DSP kernel scheduler)
    uint32_t getTotalRTLSamples() const override {
        return scheduler_ ? scheduler_->getTotalLatency() : 0;
    }
    uint32_t getNodePDCDelaySamples(NodeID nodeId) const override {
        return scheduler_ ? scheduler_->getNodeLatency(nodeId) : 0;
    }

    double getSampleRate() const override { return sampleRate_.load(std::memory_order_relaxed); }

    // IAudioClient implementation (RT-Thread)
    void startCycle(uint64_t hardwareTimestamp, uint32_t cycleId) override;
    
    void processAudio(float* const* inputChannels,
                      uint32_t numInputChannels,
                      float* const* outputChannels,
                      uint32_t numOutputChannels,
                      uint32_t numFrames) override;
                      
    void endCycle(uint32_t numFrames) override;

    // Notification callbacks
    void onBufferSizeChanged(uint32_t newBufferSize) override;
    void onSampleRateChanged(uint32_t newSampleRate) override;
    void onXrun() override;
    void onDeviceDisconnected() override;

    IButlerThread* getButlerThread() override { return butlerThread_.get(); }

private:
    // Pointers to core services (not owned)
    IDSPKernel* scheduler_ = nullptr;
    ITransport* transport_ = nullptr;
    Layer2::IClockService* clock_ = nullptr;
    Layer2::IMutationBridge* mutationBridge_ = nullptr;
    Layer2::IEventQueue* eventQueue_ = nullptr;
    Layer2::ITelemetryBridge* telemetryBridge_ = nullptr;
    Layer2::ITempoService* tempoService_ = nullptr;
    Layer1::IMIDIDriver* midiDriver_ = nullptr;
    IMIDIPlayheadRenderer* midiPlayheadRenderer_ = nullptr;
    const IMidiClipDataProvider* midiClipDataProvider_ = nullptr;
    IAutomationProcessor* automationProcessor_ = nullptr;

    // Internal state
    std::atomic<double> sampleRate_{48000.0};
    std::atomic<uint32_t> maxBlockSize_{512};
    std::atomic<uint64_t> totalCycles_{0};
    std::atomic<double> cpuLoad_{0.0};
    std::atomic<uint32_t> xrunCount_{0};
    std::atomic<bool> offlineExportActive_{false};

    std::unique_ptr<IButlerThread> butlerThread_;
    std::unique_ptr<OfflineDSPThreadPool> offlineDSPThreadPool_;
    std::unique_ptr<PreBaker> preBaker_;
    
    // Process context for the current cycle
    ProcessContext currentContext_;

    struct MetronomeVoice {
        bool active = false;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        float amplitude = 0.0f;
        float decayFactor = 0.995f;
        uint32_t samplesProcessed = 0;
        uint32_t totalDurationSamples = 0;
        uint32_t triggerOffset = 0;
    } metronomeVoice_;
};

} // namespace Layer3
