#include "sampler_node.h"
#include "common/dsp/event_scanner.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include <iostream>

namespace DSP {

Layer1::IFileSystem* SamplerFactory::s_fileSystem = nullptr;

static auto& s_registry = SamplerFactory::getRegistry();

void processSampler(
    NodeID nodeId,
    float* const* /*inputs*/,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const EventData* events,
    uint32_t numEvents,
    EventData* /*outEvents*/,
    uint32_t* /*outEventCount*/,
    const ProcessContext* context,
    const bool* /*inputSilence*/,
    bool* /*isOutputSilent*/
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || !s->buffer || numSamples == 0) return;

    // Update isPlaying based on ProcessContext transport state if available
    if (context) {
        s->isPlaying = (context->transportState == TransportState::PLAYING);
    }

    EventScanner scanner(events, numEvents);
    
    // Trigger refill request for the active position
    if (context) {
        int64_t transportPos = static_cast<int64_t>(context->transport.positionSample);
        int64_t playbackPos = static_cast<int64_t>(s->playbackPosition);
        if (transportPos >= playbackPos || transportPos + numSamples > playbackPos) {
            uint64_t readPos = (transportPos >= playbackPos) ? static_cast<uint64_t>(transportPos - playbackPos) : 0;
            s->buffer->requestRefill(readPos);
            if (context->isOffline && SamplerFactory::s_fileSystem) {
                s->buffer->refillAsync(readPos, SamplerFactory::s_fileSystem);
            }
        }
    } else {
        s->buffer->requestRefill(s->playbackPosition);
    }

    // Determine active playback position and play status at the block level
    uint64_t startPos = 0;
    uint32_t activeSamples = numSamples;
    uint32_t outputOffset = 0;
    bool shouldPlay = false;

    if (context) {
        int64_t transportPos = static_cast<int64_t>(context->transport.positionSample);
        int64_t playbackPos = static_cast<int64_t>(s->playbackPosition);
        
        if (transportPos >= playbackPos) {
            startPos = static_cast<uint64_t>(transportPos - playbackPos);
            activeSamples = numSamples;
            outputOffset = 0;
            shouldPlay = true;
        } else if (transportPos + numSamples > playbackPos) {
            startPos = 0;
            outputOffset = static_cast<uint32_t>(playbackPos - transportPos);
            activeSamples = numSamples - outputOffset;
            shouldPlay = true;
        }
    } else {
        startPos = s->playbackPosition;
        activeSamples = numSamples;
        outputOffset = 0;
        shouldPlay = true;
    }

    // Handle initial state if transport is stopped
    if (!s->isPlaying || !shouldPlay) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            std::memset(outputs[ch], 0, numSamples * sizeof(float));
        }
        return;
    }

    // Determine block segments & wrap-around
    uint32_t capacity = s->buffer->getTotalCapacity();
    uint32_t offset = static_cast<uint32_t>(startPos % capacity);
    
    const float* planarBuffer1Local[64] = { nullptr };
    const float* planarBuffer2Local[64] = { nullptr };
    const float* const* planarBuffer1 = nullptr;
    const float* const* planarBuffer2 = nullptr;
    uint32_t L1 = activeSamples;
    uint32_t L2 = 0;
    uint32_t srcChannels = s->buffer->getNumChannels();
    uint32_t channelsToCopy = std::min(srcChannels, 64U);

    if (offset + activeSamples > capacity) {
        L1 = capacity - offset;
        L2 = activeSamples - L1;
        
        const float* const* ptrs1 = s->buffer->getRTBuffer(startPos);
        if (ptrs1) {
            std::memcpy(planarBuffer1Local, ptrs1, channelsToCopy * sizeof(float*));
            planarBuffer1 = planarBuffer1Local;
        }
        
        const float* const* ptrs2 = s->buffer->getRTBuffer(startPos + L1);
        if (ptrs2) {
            std::memcpy(planarBuffer2Local, ptrs2, channelsToCopy * sizeof(float*));
            planarBuffer2 = planarBuffer2Local;
        }
    } else {
        const float* const* ptrs1 = s->buffer->getRTBuffer(startPos);
        if (ptrs1) {
            std::memcpy(planarBuffer1Local, ptrs1, channelsToCopy * sizeof(float*));
            planarBuffer1 = planarBuffer1Local;
        }
    }

    // Loop 1: Pre-clip offset (silence, but scan events)
    for (uint32_t blockIdx = 0; blockIdx < outputOffset; ++blockIdx) {
        scanner.processEventsAtOffset(blockIdx, [&](const EventData& e) {
            if (e.eventType == EventType::TRANSPORT) {
                s->isPlaying = (e.payload.transport.transportState == 1);
            } else if (e.eventType == EventType::AUTOMATION) {
                if (e.payload.automation.parameterIndex == 0) {
                    s->targetGain = e.payload.automation.targetValue;
                }
            }
        });
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            outputs[ch][blockIdx] = 0.0f;
        }
    }

    // Loop 2: Segment 1 of audio data
    for (uint32_t i = 0; i < L1; ++i) {
        uint32_t blockIdx = outputOffset + i;
        scanner.processEventsAtOffset(blockIdx, [&](const EventData& e) {
            if (e.eventType == EventType::TRANSPORT) {
                s->isPlaying = (e.payload.transport.transportState == 1);
            } else if (e.eventType == EventType::AUTOMATION) {
                if (e.payload.automation.parameterIndex == 0) {
                    s->targetGain = e.payload.automation.targetValue;
                }
            }
        });

        if (!s->isPlaying) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                outputs[ch][blockIdx] = 0.0f;
            }
            continue;
        }

        if (planarBuffer1) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                uint32_t srcCh = (ch < srcChannels) ? ch : 0;
                outputs[ch][blockIdx] = planarBuffer1[srcCh][i] * s->targetGain;
            }
        } else {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                outputs[ch][blockIdx] = 0.0f;
            }
        }
    }

    // Loop 3: Segment 2 of audio data (if any)
    for (uint32_t i = 0; i < L2; ++i) {
        uint32_t blockIdx = outputOffset + L1 + i;
        scanner.processEventsAtOffset(blockIdx, [&](const EventData& e) {
            if (e.eventType == EventType::TRANSPORT) {
                s->isPlaying = (e.payload.transport.transportState == 1);
            } else if (e.eventType == EventType::AUTOMATION) {
                if (e.payload.automation.parameterIndex == 0) {
                    s->targetGain = e.payload.automation.targetValue;
                }
            }
        });

        if (!s->isPlaying) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                outputs[ch][blockIdx] = 0.0f;
            }
            continue;
        }

        if (planarBuffer2) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                uint32_t srcCh = (ch < srcChannels) ? ch : 0;
                outputs[ch][blockIdx] = planarBuffer2[srcCh][i] * s->targetGain;
            }
        } else {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                outputs[ch][blockIdx] = 0.0f;
            }
        }
    }

    if (!context) {
        s->playbackPosition += numSamples;
    }
}


} // namespace DSP
