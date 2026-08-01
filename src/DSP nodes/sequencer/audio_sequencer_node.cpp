#include "audio_sequencer_node.h"
#include "common/dsp/event_scanner.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <numbers>

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#endif

namespace DSP {

Layer3::IButlerThread* AudioSequencerFactory::s_butlerThread = nullptr;
Layer1::IFileSystem* AudioSequencerFactory::s_fileSystem = nullptr;

static auto& s_registry = AudioSequencerFactory::getRegistry();

void processAudioSequencer(
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
    bool* isOutputSilent
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || !outputs || numSamples == 0) return;

    // 1. Initialize outputs to silence
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        if (outputs[ch]) std::memset(outputs[ch], 0, numSamples * sizeof(float));
    }

    if (!context || !context->timelineSnapshot) {
        if (isOutputSilent) {
            *isOutputSilent = true;
        }
        return;
    }

    // 2. Event-Driven Transport Check (Rule 13)
    bool isPlaying = (context->transportState == TransportState::PLAYING);
    if (!isPlaying) {
        // Still process automation events to keep s->targetGain updated
        for (uint32_t i = 0; i < numEvents; ++i) {
            const auto& e = events[i];
            if (e.eventType == EventType::AUTOMATION && e.payload.automation.parameterIndex == 0) {
                s->targetGain = e.payload.automation.targetValue;
            }
        }
        if (isOutputSilent) {
            *isOutputSilent = true;
        }
        return; // Suspends playback processing when transport is stopped
    }

    const TimelineSnapshot* snapshot = context->timelineSnapshot;
    uint64_t startSample = context->transport.positionSample;
    uint64_t endSample = startSample + numSamples;
    bool loopEnabled = context->loopEnabled;
    uint64_t loopStart = context->loopStart;
    uint64_t loopEnd = context->loopEnd;

    // 3. Block-Level Polling: Pre-filter intersecting regions (Outside Sample Loop)
    // Handle loop wrapping in intersection checks
    bool wrap = (loopEnabled && startSample + numSamples > loopEnd && startSample < loopEnd);
    uint32_t splitOffset = wrap ? static_cast<uint32_t>(loopEnd - startSample) : numSamples;
    
    uint64_t range1Start = startSample;
    uint64_t range1End = wrap ? loopEnd : endSample;
    uint64_t range2Start = loopStart;
    uint64_t range2End = loopStart + (numSamples - splitOffset);

    // Pre-allocated local array for active regions intersecting this block
    constexpr uint32_t MAX_ACTIVE_REGIONS = 16;
    const SnapshotRegion* activeRegions[MAX_ACTIVE_REGIONS];
    Layer3::IStreamingBuffer* activeBuffers[MAX_ACTIVE_REGIONS];
    uint32_t activeCount = 0;

    const SnapshotRegion* firstRegion = snapshot->regions;
    const SnapshotRegion* lastRegion = snapshot->regions + snapshot->regionCount;
    
    auto it = std::lower_bound(firstRegion, lastRegion, s->trackId,
                               [](const SnapshotRegion& reg, const TrackID& tid) {
                                   return reg.trackId.toRaw() < tid.toRaw();
                               });

    for (; it != lastRegion && it->trackId == s->trackId && activeCount < MAX_ACTIVE_REGIONS; ++it) {
        const auto& region = *it;
        if (region.type == RegionType::AUDIO && !region.isMuted) {
            uint64_t regionEnd = region.positionSample + region.durationProjectFrames;
            bool intersects = false;
            
            // Check intersection with range 1
            if (range1End > region.positionSample && range1Start < regionEnd) {
                intersects = true;
            }
            // Check intersection with range 2 (only if wrap is true)
            if (wrap && range2End > region.positionSample && range2Start < regionEnd) {
                intersects = true;
            }

            if (intersects) {
                if (AudioSequencerFactory::s_butlerThread) {
                    auto* buffer = AudioSequencerFactory::s_butlerThread->getBufferForRegion(region.regionId.toRaw(), region.sourceId);
                    if (buffer) {
                        activeRegions[activeCount] = &region;
                        activeBuffers[activeCount] = buffer;
                        activeCount++;
                    }
                }
            }
        }
    }

    // 4. Short-Circuit if no regions intersect the current block
    if (activeCount == 0) {
        if (isOutputSilent) {
            *isOutputSilent = true;
        }
        return;
    }

    if (isOutputSilent) {
        *isOutputSilent = false;
    }

    // 5. Sample processing (Loops only over active, intersecting regions)
    EventScanner scanner(events, numEvents);

    auto processSegment = [&](uint32_t startOffset, uint32_t segmentSize, uint64_t segmentPlayheadStart) {
        uint32_t processed = 0;
        while (processed < segmentSize) {
            uint32_t currentOffset = startOffset + processed;
            uint32_t remainingInSegment = segmentSize - processed;
            
            // A. Sub-Block Slicing for Sample-Accurate Events
            uint32_t subBlockSize = remainingInSegment;
            const EventData* nextEvent = scanner.peekNextEvent();
            if (nextEvent) {
                if (nextEvent->sampleOffset > currentOffset) {
                    uint32_t distance = nextEvent->sampleOffset - currentOffset;
                    if (distance < subBlockSize) {
                        subBlockSize = distance;
                    }
                } else {
                    scanner.processEventsAtOffset(currentOffset, [&](const EventData& e) {
                        if (e.eventType == EventType::AUTOMATION && e.payload.automation.parameterIndex == 0) {
                            s->targetGain = e.payload.automation.targetValue;
                        }
                    });
                    nextEvent = scanner.peekNextEvent();
                    if (nextEvent && nextEvent->sampleOffset > currentOffset) {
                        uint32_t distance = nextEvent->sampleOffset - currentOffset;
                        if (distance < subBlockSize) {
                            subBlockSize = distance;
                        }
                    }
                }
            }

            uint64_t subBlockPlayheadStart = segmentPlayheadStart + processed;
            
            for (uint32_t r = 0; r < activeCount; ++r) {
                const auto& region = *activeRegions[r];
                auto* buffer = activeBuffers[r];
                
                uint64_t regStart = region.positionSample;
                uint64_t regEnd = regStart + region.durationProjectFrames;
                
                uint64_t subBlockStartPlayhead = subBlockPlayheadStart;
                uint64_t subBlockEndPlayhead = subBlockPlayheadStart + subBlockSize;
                
                if (subBlockEndPlayhead > regStart && subBlockStartPlayhead < regEnd) {
                    uint64_t intersectStartPlayhead = std::max(subBlockStartPlayhead, regStart);
                    uint64_t intersectEndPlayhead = std::min(subBlockEndPlayhead, regEnd);
                    
                    uint32_t intersectOffsetInSubBlock = static_cast<uint32_t>(intersectStartPlayhead - subBlockStartPlayhead);
                    uint32_t intersectSize = static_cast<uint32_t>(intersectEndPlayhead - intersectStartPlayhead);
                    
                    uint32_t writeOffset = currentOffset + intersectOffsetInSubBlock;
                    uint64_t readIndexStart = (intersectStartPlayhead - regStart);
                    
                    // B. Batched Refill Requests
                    if (intersectSize > 0) {
                        uint64_t lastReadIndex = readIndexStart + intersectSize - 1;
                        if (context->isOffline && AudioSequencerFactory::s_fileSystem) {
                            buffer->requestRefill(lastReadIndex);
                            buffer->refillAsync(readIndexStart, AudioSequencerFactory::s_fileSystem);
                        } else {
                            buffer->requestRefill(lastReadIndex);
                        }
                    }
                    
                    // C. Ring Buffer Wrap Handling & Vectorized Mixing
                    uint32_t bufferCapacity = buffer->getTotalCapacity();
                    uint32_t numSrcChannels = buffer->getNumChannels();
                    float gain = region.gain * s->targetGain;
                    
                    uint32_t framesProcessed = 0;
                    while (framesProcessed < intersectSize) {
                        uint64_t currentReadIndex = readIndexStart + framesProcessed;
                        uint32_t offsetInRing = static_cast<uint32_t>(currentReadIndex % bufferCapacity);
                        uint32_t framesToEndOfRing = bufferCapacity - offsetInRing;
                        uint32_t framesToProcess = std::min(intersectSize - framesProcessed, framesToEndOfRing);
                        
                        const float* const* planarBuffer = buffer->getRTBuffer(currentReadIndex);
                        if (planarBuffer) {
                            uint32_t destOffset = writeOffset + framesProcessed;
                            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                                if (!outputs[ch]) continue;
                                uint32_t srcCh = (ch < numSrcChannels) ? ch : 0;
                                const float* srcPtr = planarBuffer[srcCh];
                                float* destPtr = &outputs[ch][destOffset];
                                
                                if (region.fadeInSamples == 0 && region.fadeOutSamples == 0) {
                                    #if defined(__APPLE__)
                                    vDSP_vsma(srcPtr, 1, &gain, destPtr, 1, destPtr, 1, framesToProcess);
                                    #else
                                    for (uint32_t sampleIdx = 0; sampleIdx < framesToProcess; ++sampleIdx) {
                                        destPtr[sampleIdx] += srcPtr[sampleIdx] * gain;
                                    }
                                    #endif
                                } else {
                                    for (uint32_t sampleIdx = 0; sampleIdx < framesToProcess; ++sampleIdx) {
                                        uint64_t timelinePos = intersectStartPlayhead + framesProcessed + sampleIdx;
                                        float fadeGain = 1.0f;
                                        if (region.fadeInSamples > 0 && timelinePos < regStart + region.fadeInSamples) {
                                            const float norm = static_cast<float>(timelinePos - regStart) / static_cast<float>(region.fadeInSamples);
                                            fadeGain = std::sin(norm * MathConstants::HALF_PI);
                                        } else if (region.fadeOutSamples > 0 && timelinePos >= regEnd - region.fadeOutSamples) {
                                            if (timelinePos < regEnd) {
                                                const float norm = static_cast<float>(regEnd - timelinePos) / static_cast<float>(region.fadeOutSamples);
                                                fadeGain = std::sin(norm * MathConstants::HALF_PI);
                                            } else {
                                                fadeGain = 0.0f;
                                            }
                                        }
                                        destPtr[sampleIdx] += srcPtr[sampleIdx] * gain * fadeGain;
                                    }
                                }
                            }
                        }
                        framesProcessed += framesToProcess;
                    }
                }
            }
            processed += subBlockSize;
        }
    };

    if (wrap) {
        processSegment(0, splitOffset, startSample);
        processSegment(splitOffset, numSamples - splitOffset, loopStart);
    } else {
        processSegment(0, numSamples, startSample);
    }
}

} // namespace DSP
