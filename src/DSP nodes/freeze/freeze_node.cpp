#include "freeze_node.h"
#include "common/dsp/event_scanner.h"
#include "common/math/vector.h"
#include "Core audio engine/streaming/ibutler_thread.h" // For IStreamingBuffer

namespace DSP {

static auto& s_registry = FreezeFactory::getRegistry();

void processFreeze(
    NodeID nodeId,
    float* const* inputs,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const EventData* events,
    uint32_t numEvents,
    EventData* /*outEvents*/,
    uint32_t* /*outEventCount*/,
    const ProcessContext* /*context*/
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || !inputs || !outputs || numSamples == 0) return;

    // 2. Sample-Processing Loop
    uint32_t capacity = s->buffer ? s->buffer->getCapacity() : 0;
    float* const* bufferData = s->buffer ? s->buffer->getRTBuffer() : nullptr;

    for (uint32_t i = 0; i < numSamples; ++i) {
        
        // Handle Events at this sample offset
        scanner.processEventsAtOffset(i, [&](const EventData& e) {
            if (e.eventType == EventType::AUTOMATION) {
                if (e.payload.automation.parameterIndex == 0) { // Mode parameter
                    s->mode = static_cast<FreezeState::Mode>(e.payload.automation.targetValue);
                    if (s->mode == FreezeState::Mode::Record) {
                        s->recordedLength = 0;
                        s->position = 0;
                    }
                }
            }
        });

        // Logic based on Mode
        switch (s->mode) {
            case FreezeState::Mode::Bypass:
                for (uint32_t ch = 0; ch < numChannels; ++ch) {
                    outputs[ch][i] = inputs[ch][i];
                }
                break;

            case FreezeState::Mode::Record:
                if (bufferData) {
                    uint64_t writeIdx = s->buffer->getWritePosition();
                    for (uint32_t ch = 0; ch < numChannels; ++ch) {
                        float sample = inputs[ch][i];
                        bufferData[ch][writeIdx % capacity] = sample;
                        outputs[ch][i] = sample;
                    }
                    s->buffer->advanceWritePosition(1);
                    s->recordedLength++;
                } else {
                    for (uint32_t ch = 0; ch < numChannels; ++ch) {
                        outputs[ch][i] = inputs[ch][i];
                    }
                }
                break;

            case FreezeState::Mode::Playback:
                if (bufferData && s->recordedLength > 0) {
                    uint64_t readIdx = s->position % s->recordedLength;
                    for (uint32_t ch = 0; ch < numChannels; ++ch) {
                        outputs[ch][i] = bufferData[ch][readIdx % capacity];
                    }
                    s->position++;
                } else {
                    for (uint32_t ch = 0; ch < numChannels; ++ch) {
                        outputs[ch][i] = 0.0f;
                    }
                }
                break;
        }
    }

    // 3. Final Sanitization
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        Math::Vector::sanitize(outputs[ch], numSamples);
    }
}

} // namespace DSP
