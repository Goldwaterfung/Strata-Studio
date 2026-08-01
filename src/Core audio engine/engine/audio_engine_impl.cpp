// src/Core audio engine/engine/audio_engine_impl.cpp
#include "Core audio engine/engine/audio_engine_impl.h"
#include "Core audio engine/automation/iautomation_processor.h"
#include "common/math/primitives.h"
#include <cstring>
#include <chrono>

namespace Layer3 {

std::unique_ptr<IAudioEngine> IAudioEngine::create() {
    return std::make_unique<AudioEngineImpl>();
}

AudioEngineImpl::AudioEngineImpl() {
    std::memset(&currentContext_, 0, sizeof(ProcessContext));
    butlerThread_ = IButlerThread::create();
    offlineDSPThreadPool_ = std::make_unique<OfflineDSPThreadPool>(this, 4); // Default to max 4 threads
    preBaker_ = std::make_unique<PreBaker>(offlineDSPThreadPool_.get());
}

AudioEngineImpl::~AudioEngineImpl() = default;

void AudioEngineImpl::setMidiClipDataProvider(const IMidiClipDataProvider* provider) {
    midiClipDataProvider_ = provider;
}

void AudioEngineImpl::setAutomationProcessor(IAutomationProcessor* processor) {
    automationProcessor_ = processor;
}

void AudioEngineImpl::setOfflineExportActive(bool active) {
    offlineExportActive_.store(active, std::memory_order_release);
}

void AudioEngineImpl::prepare(double sampleRate, uint32_t maxBlockSize, Layer1::WorkgroupHandle workgroup) {
    sampleRate_.store(sampleRate);
    maxBlockSize_.store(maxBlockSize);
    
    if (butlerThread_) {
        butlerThread_->setSampleRate(static_cast<float>(sampleRate));
        butlerThread_->start(workgroup);
    }
}

void AudioEngineImpl::reset() {
    totalCycles_.store(0);
    cpuLoad_.store(0.0);
    xrunCount_.store(0);
}

TransportState AudioEngineImpl::getTransportState() const {
    if (transport_) {
        return transport_->getState();
    }
    return TransportState::STOPPED;
}

uint64_t AudioEngineImpl::getTransportPosition() const {
    if (transport_) {
        return transport_->getPosition();
    }
    return 0;
}

//==============================================================================
// PHASE 1: START CYCLE (Capture & Sync)
//==============================================================================

void AudioEngineImpl::startCycle(uint64_t hardwareTimestamp, uint32_t cycleId) {
    // 1. Latch high-resolution clock with actual block size capacity
    uint32_t currentMaxBlockSize = maxBlockSize_.load(std::memory_order_relaxed);
    if (clock_) {
        clock_->startCycle(hardwareTimestamp, currentMaxBlockSize);
    }

    // 2. Latch topology and events for deterministic processing
    if (mutationBridge_) {
        mutationBridge_->prepareCycle();
    }
    
    if (eventQueue_) {
        eventQueue_->prepareCycle();
    }

    // 3. Pull MIDI events from Layer 1 Drivers (RT-SAFE)
    if (midiDriver_ && eventQueue_) {
        Layer1::MIDIMessage hardwareMsg;
        while (midiDriver_->popMIDIEvent(hardwareMsg)) {
            // Convert Layer 1 MIDIMessage to Layer 0 EventData
            EventData event = {};
            event.eventType = EventType::MIDI_NOTE_ON; // Default
            
            // Calculate exact sub-sample offset within current block
            uint32_t offset = clock_ ? clock_->getOffsetForTimestamp(hardwareMsg.timestamp) : 0;
            event.sampleOffset = (offset < currentMaxBlockSize) ? offset : (currentMaxBlockSize - 1);
            
            event.targetNodeId = NodeID::invalid(); // Broadcast
            
            // Map raw MIDI data
            if (hardwareMsg.size >= 1) {
                uint8_t status = hardwareMsg.data[0];
                uint8_t type = status & 0xF0;
                
                if (type == 0x90 || type == 0x80) { // Note On/Off
                    event.eventType = (type == 0x90 && hardwareMsg.data[2] > 0) ? EventType::MIDI_NOTE_ON : EventType::MIDI_NOTE_OFF;
                    event.payload.midiNote.pitch = hardwareMsg.data[1];
                    event.payload.midiNote.velocity = (type == 0x90) ? hardwareMsg.data[2] : 0;
                    event.payload.midiNote.channel = status & 0x0F;
                } else if (type == 0xB0) { // CC
                    event.eventType = EventType::MIDI_CC;
                    event.payload.midiCC.controllerNumber = hardwareMsg.data[1];
                    event.payload.midiCC.value = hardwareMsg.data[2];
                    event.payload.midiCC.channel = status & 0x0F;
                }
                // ... map other types
            }
            
            eventQueue_->pushEvent(event);
        }
    }

    // 4. Update Transport & Tempo Context
    if (transport_) {
        currentContext_.transport = transport_->getDetailedPosition();
        currentContext_.transportState = transport_->getState();
        LoopState loop = transport_->getLoopState();
        currentContext_.loopEnabled = (loop.mode == LoopState::LoopMode::ENABLED);
        currentContext_.loopStart = loop.startSample;
        currentContext_.loopEnd = loop.endSample;
        currentContext_.metronomeEnabled = transport_->isMetronomeEnabled();
    } else {
        currentContext_.loopEnabled = false;
        currentContext_.loopStart = 0;
        currentContext_.loopEnd = 0;
        currentContext_.metronomeEnabled = false;
    }
    
    if (tempoService_) {
        currentContext_.sampleRate = static_cast<float>(sampleRate_.load(std::memory_order_relaxed));
        tempoService_->updateForCycle(currentContext_);
    }

    // 5. Update ProcessContext for the current cycle
    currentContext_.hardwareTimestamp = hardwareTimestamp;
    currentContext_.cycleId = cycleId;
    currentContext_.sampleRate = static_cast<float>(sampleRate_.load(std::memory_order_relaxed));
    currentContext_.timelineSnapshot = scheduler_ ? scheduler_->getActiveTimelineSnapshot() : nullptr;
    currentContext_.midiClipDataProvider = midiClipDataProvider_;
}

//==============================================================================
// PHASE 2: PROCESS AUDIO (DSP Traversal)
//==============================================================================

void AudioEngineImpl::processAudio(float* const* inputChannels,
                                  uint32_t numInputChannels,
                                  float* const* outputChannels,
                                  uint32_t numOutputChannels,
                                  uint32_t numFrames) 
{
    Math::setupFPU();
    currentContext_.inputChannels = inputChannels;
    currentContext_.numInputChannels = numInputChannels;
    auto startTime = std::chrono::high_resolution_clock::now();

    // Store frame count for Phase 3 advancement
    currentContext_.currentBlockSize = numFrames;

    if (midiPlayheadRenderer_ && eventQueue_) {
        uint64_t startSample = transport_ ? transport_->getPosition() : 0;
        LoopState loop = transport_ ? transport_->getLoopState() : LoopState{};
        bool loopEnabled = (loop.mode == LoopState::LoopMode::ENABLED);
        bool isPlaying = currentContext_.transportState == TransportState::PLAYING;
        midiPlayheadRenderer_->renderMIDIPlayback(startSample, numFrames, loopEnabled, loop.startSample, loop.endSample, eventQueue_, isPlaying);
    }

    if (automationProcessor_ && eventQueue_) {
        constexpr uint32_t MAX_AUTOMATION_EVENTS = 512;
        EventData automationEvents[MAX_AUTOMATION_EVENTS];
        uint64_t startSample = transport_ ? transport_->getPosition() : 0;
        bool isPlaying = currentContext_.transportState == TransportState::PLAYING;
        uint32_t generatedCount = automationProcessor_->generateAutomationEvents(
            startSample,
            numFrames,
            automationEvents,
            MAX_AUTOMATION_EVENTS,
            isPlaying
        );
        for (uint32_t i = 0; i < generatedCount; ++i) {
            eventQueue_->pushEvent(automationEvents[i]);
        }
    }

    if (scheduler_ && !offlineExportActive_.load(std::memory_order_acquire)) {
        // Execute the DSP graph using the latched context
        scheduler_->process(inputChannels, outputChannels, 
                           numOutputChannels, numFrames, 
                           &currentContext_);
    } else {
        // Fallback: Zero output if no scheduler attached or offline export is active
        for (uint32_t i = 0; i < numOutputChannels; ++i) {
            std::memset(outputChannels[i], 0, numFrames * sizeof(float));
        }
    }

    // Mix metronome clicks
    if (currentContext_.metronomeEnabled && currentContext_.transportState != TransportState::STOPPED) {
        constexpr double PI = 3.14159265358979323846;
        float sampleRateF = currentContext_.sampleRate;
        uint64_t startSample = transport_ ? transport_->getPosition() : 0;
        double bStart = transport_ ? transport_->samplesToBeats(startSample) : 0.0;
        double bEnd = transport_ ? transport_->samplesToBeats(startSample + numFrames) : 0.0;

        int startBeat = static_cast<int>(std::ceil(bStart));
        int endBeat = static_cast<int>(std::floor(bEnd));

        uint32_t triggerOffsets[4];
        bool triggerIsDownbeat[4];
        uint32_t triggerCount = 0;

        for (int b = startBeat; b <= endBeat; ++b) {
            uint64_t beatSample = transport_ ? transport_->beatsToSamples(b) : 0;
            if (beatSample >= startSample && beatSample < startSample + numFrames) {
                if (triggerCount < 4) {
                    triggerOffsets[triggerCount] = static_cast<uint32_t>(beatSample - startSample);
                    triggerIsDownbeat[triggerCount] = (b % currentContext_.transport.numerator) == 0;
                    triggerCount++;
                }
            }
        }

        uint32_t currentTriggerIdx = 0;
        for (uint32_t sample = 0; sample < numFrames; ++sample) {
            if (currentTriggerIdx < triggerCount && sample == triggerOffsets[currentTriggerIdx]) {
                metronomeVoice_.active = true;
                metronomeVoice_.phase = 0.0;
                double freq = triggerIsDownbeat[currentTriggerIdx] ? 800.0 : 500.0;
                metronomeVoice_.phaseIncrement = (2.0 * PI * freq) / static_cast<double>(sampleRateF);
                metronomeVoice_.amplitude = 0.25f; // -12 dBFS
                metronomeVoice_.decayFactor = std::pow(0.001f, 1.0f / (0.05f * sampleRateF)); // 50ms decay to -60dB
                metronomeVoice_.samplesProcessed = 0;
                metronomeVoice_.totalDurationSamples = static_cast<uint32_t>(0.05f * sampleRateF);
                currentTriggerIdx++;
            }

            if (metronomeVoice_.active) {
                float val = static_cast<float>(std::sin(metronomeVoice_.phase)) * metronomeVoice_.amplitude;
                for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
                    if (outputChannels[ch]) {
                        outputChannels[ch][sample] += val;
                    }
                }
                metronomeVoice_.phase += metronomeVoice_.phaseIncrement;
                if (metronomeVoice_.phase >= 2.0 * PI) {
                    metronomeVoice_.phase -= 2.0 * PI;
                }
                metronomeVoice_.amplitude *= metronomeVoice_.decayFactor;
                metronomeVoice_.samplesProcessed++;
                if (metronomeVoice_.samplesProcessed >= metronomeVoice_.totalDurationSamples) {
                    metronomeVoice_.active = false;
                }
            }
        }
    } else {
        metronomeVoice_.active = false;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double processDurationSeconds = std::chrono::duration<double>(endTime - startTime).count();
    double blockDurationSeconds = static_cast<double>(numFrames) / sampleRate_.load(std::memory_order_relaxed);

    double instantLoad = 0.0;
    if (blockDurationSeconds > 0.0) {
        instantLoad = processDurationSeconds / blockDurationSeconds;
    }

    // Exponential Moving Average (EMA) for smooth UI metering, alpha = 0.1
    double currentLoad = cpuLoad_.load(std::memory_order_relaxed);
    double nextLoad = currentLoad + 0.1 * (instantLoad - currentLoad);
    cpuLoad_.store(nextLoad, std::memory_order_relaxed);
}

//==============================================================================
// PHASE 3: END CYCLE (Safety & Advancement)
//==============================================================================

void AudioEngineImpl::endCycle(uint32_t numFrames) {
    // 1. Advance transport position for next cycle
    if (transport_) {
        transport_->advancePosition(numFrames);
    }

    // 2. Signal the butler thread to process any pending streaming buffer refills.
    // processSampler() calls requestRefill() on the RT thread, which sets refillRequested=true.
    // The butler thread blocks on a semaphore and will only process those requests when
    // woken here. dispatch_semaphore_signal is async-signal-safe and RT-safe.
    if (butlerThread_) {
        uint64_t pos = transport_ ? transport_->getPosition() : 0;
        bool isPlaying = currentContext_.transportState == TransportState::PLAYING;
        float sr = currentContext_.sampleRate;
        butlerThread_->updateTransportState(pos, sr, isPlaying);
        butlerThread_->updateTimelineSnapshot(currentContext_.timelineSnapshot);
        butlerThread_->wakeButler();
    }

    // 3. Increment cycle counter
    totalCycles_.fetch_add(1, std::memory_order_relaxed);
}

//==============================================================================
// Notification Callbacks
//==============================================================================

void AudioEngineImpl::onBufferSizeChanged(uint32_t newBufferSize) {
    maxBlockSize_.store(newBufferSize);
}

void AudioEngineImpl::onSampleRateChanged(uint32_t newSampleRate) {
    sampleRate_.store(static_cast<double>(newSampleRate));
}

void AudioEngineImpl::onXrun() {
    xrunCount_.fetch_add(1, std::memory_order_relaxed);
    // Push Xrun telemetry if bridge exists
    if (telemetryBridge_) {
        // frame.type = XRUN_DETECTED...
    }
}

void AudioEngineImpl::onDeviceDisconnected() {
    // Emergency stop
    if (transport_) {
        transport_->stop();
    }
}

} // namespace Layer3
