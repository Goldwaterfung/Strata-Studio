// src/Core audio engine/automation/automation_processor_impl.cpp

#include "automation_processor_impl.h"
#include "common/dsp/curve_interpolation.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <mutex>

namespace Layer3 {

//==============================================================================
// AUTOMATION LANE IMPLEMENTATION
//==============================================================================

AutomationLane::AutomationLane(NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex)
    : targetNodeId_(targetNodeId)
    , subNodeId_(subNodeId)
    , parameterIndex_(parameterIndex)
    , recordingBuffer_(std::make_unique<RecordingBuffer>())
    , lastValue_(0.0f)
    , lastPosition_(std::numeric_limits<uint64_t>::max())
{
    std::fill(playbackPointsCount_, playbackPointsCount_ + 2, 0);
    if (targetNodeId.isValid()) {
        if (parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Volume)) {
            lastValue_.store(Math::Gain::UNITY_NORMALIZED, std::memory_order_relaxed);
        } else if (parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Pan)) {
            lastValue_.store(Math::Gain::CENTER_PAN_NORMALIZED, std::memory_order_relaxed);
        }
    }
}

AutomationLane::~AutomationLane() = default;

AutomationLane::AutomationLane(AutomationLane&& other) noexcept
    : targetNodeId_(other.targetNodeId_)
    , subNodeId_(other.subNodeId_)
    , parameterIndex_(other.parameterIndex_)
    , recordingBuffer_(std::move(other.recordingBuffer_))
    , lastValue_(other.lastValue_.load(std::memory_order_relaxed))
    , lastPosition_(other.lastPosition_.load(std::memory_order_relaxed))
    , activeBufferIndex_(other.activeBufferIndex_.load(std::memory_order_relaxed))
{
    playbackPoints_[0] = std::move(other.playbackPoints_[0]);
    playbackPoints_[1] = std::move(other.playbackPoints_[1]);
    playbackPointsCount_[0] = other.playbackPointsCount_[0];
    playbackPointsCount_[1] = other.playbackPointsCount_[1];
}

AutomationLane& AutomationLane::operator=(AutomationLane&& other) noexcept {
    if (this != &other) {
        targetNodeId_ = other.targetNodeId_;
        subNodeId_ = other.subNodeId_;
        parameterIndex_ = other.parameterIndex_;
        recordingBuffer_ = std::move(other.recordingBuffer_);
        lastValue_.store(other.lastValue_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        lastPosition_.store(other.lastPosition_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        playbackPoints_[0] = std::move(other.playbackPoints_[0]);
        playbackPoints_[1] = std::move(other.playbackPoints_[1]);
        playbackPointsCount_[0] = other.playbackPointsCount_[0];
        playbackPointsCount_[1] = other.playbackPointsCount_[1];
        activeBufferIndex_.store(other.activeBufferIndex_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

void AutomationLane::recordValue(float value, uint64_t position) {
    ::AutomationPoint point{position, value, ::AutomationPoint::Shape::LINEAR, 0.0f};

    // Push to recording buffer (may drop if full, which is acceptable for automation)
    recordingBuffer_->push(point);

    // Update last known state
    lastValue_.store(value, std::memory_order_relaxed);
    lastPosition_.store(position, std::memory_order_relaxed);
}

uint32_t AutomationLane::generateEvents(
    uint64_t startPosition,
    uint32_t numSamples,
    EventData* outEvents,
    uint32_t maxEvents,
    bool isPlaying
) const {
    if (!outEvents || maxEvents == 0) {
        return 0;
    }

    uint32_t eventCount = 0;
    uint64_t endPosition = startPosition + numSamples;

    // Load active double-buffered index
    uint32_t activeIdx = activeBufferIndex_.load(std::memory_order_acquire);
    const auto& points = playbackPoints_[activeIdx];
    uint32_t count = playbackPointsCount_[activeIdx];

    if (count == 0) {
        lastPosition_.store(startPosition + numSamples, std::memory_order_relaxed);
        return 0;
    }

    // Helper to evaluate value at a specific position (RT-safe O(log N))
    auto getValueAt = [&](uint64_t pos) -> float {
        if (count == 0) {
            return lastValue_.load(std::memory_order_relaxed);
        }
        if (pos <= points[0].positionSample) {
            return points[0].value;
        }
        if (pos >= points[count - 1].positionSample) {
            return points[count - 1].value;
        }

        auto it_seg = std::lower_bound(points.begin(), points.begin() + count, pos,
            [](const ::AutomationPoint& pt, uint64_t p) {
                return pt.positionSample < p;
            });

        uint32_t idx = static_cast<uint32_t>(std::distance(points.begin(), it_seg));
        if (points[idx].positionSample == pos) {
            return points[idx].value;
        }
        return DSP::CurveInterpolator::calculate(points[idx - 1], points[idx], pos);
    };

    bool seekOccurred = (startPosition != lastPosition_.load(std::memory_order_relaxed));

    if (!isPlaying) {
        if (seekOccurred) {
            float val = getValueAt(startPosition);
            EventData event{};
            event.targetNodeId = NodeID::invalid(); // Filled by caller
            event.sampleOffset = 0;
            event.eventType = EventType::AUTOMATION;
            event.payload.automation.parameterIndex = parameterIndex_;
            event.payload.automation.targetValue = val;
            event.payload.automation.rampDuration = 0;
            outEvents[eventCount++] = event;
            lastValue_.store(val, std::memory_order_relaxed);
            lastPosition_.store(startPosition, std::memory_order_relaxed);
        }
        return eventCount;
    }

    // Output snap event if seeking/discontinuity occurred
    if (seekOccurred) {
        float val = getValueAt(startPosition);
        EventData event{};
        event.targetNodeId = NodeID::invalid();
        event.sampleOffset = 0;
        event.eventType = EventType::AUTOMATION;
        event.payload.automation.parameterIndex = parameterIndex_;
        event.payload.automation.targetValue = val;
        event.payload.automation.rampDuration = 0;
        outEvents[eventCount++] = event;
        lastValue_.store(val, std::memory_order_relaxed);
    }

    // Collect partition points within [startPosition, endPosition]
    std::array<uint64_t, 64> partitions;
    uint32_t partitionCount = 0;
    partitions[partitionCount++] = startPosition;

    // Find first point > startPosition
    auto it = std::upper_bound(points.begin(), points.begin() + count, startPosition,
        [](uint64_t pos, const ::AutomationPoint& pt) {
            return pos < pt.positionSample;
        });

    while (it != points.begin() + count && it->positionSample < endPosition) {
        if (partitionCount < partitions.size() - 1) {
            partitions[partitionCount++] = it->positionSample;
        }
        ++it;
    }
    partitions[partitionCount++] = endPosition;

    // Process each partition segment
    for (uint32_t i = 0; i < partitionCount - 1; ++i) {
        uint64_t segStart = partitions[i];
        uint64_t segEnd = partitions[i + 1];

        if (count == 0) {
            continue;
        }

        auto it_to = std::lower_bound(points.begin(), points.begin() + count, segEnd,
            [](const ::AutomationPoint& pt, uint64_t pos) {
                return pt.positionSample < pos;
            });

        if (it_to == points.begin() + count) {
            // After last point: flat value
            continue;
        }

        if (it_to == points.begin()) {
            // Before first point: flat value
            if (segEnd == points[0].positionSample) {
                if (eventCount < maxEvents) {
                    EventData event{};
                    event.targetNodeId = NodeID::invalid();
                    event.sampleOffset = static_cast<uint32_t>(segEnd - startPosition);
                    event.eventType = EventType::AUTOMATION;
                    event.payload.automation.parameterIndex = parameterIndex_;
                    event.payload.automation.targetValue = points[0].value;
                    event.payload.automation.rampDuration = 0;
                    outEvents[eventCount++] = event;
                }
            }
            continue;
        }

        const auto& p_to = *it_to;
        const auto& p_from = *(it_to - 1);

        if (p_from.curveShape == ::AutomationPoint::Shape::STEP) {
            if (segEnd == p_to.positionSample) {
                if (eventCount < maxEvents) {
                    EventData event{};
                    event.targetNodeId = NodeID::invalid();
                    event.sampleOffset = static_cast<uint32_t>(segEnd - startPosition);
                    event.eventType = EventType::AUTOMATION;
                    event.payload.automation.parameterIndex = parameterIndex_;
                    event.payload.automation.targetValue = p_to.value;
                    event.payload.automation.rampDuration = 0;
                    outEvents[eventCount++] = event;
                }
            }
        } else if (p_from.curveShape == ::AutomationPoint::Shape::SQUARE) {
            uint64_t mid = p_from.positionSample + (p_to.positionSample - p_from.positionSample) / 2;
            if (mid >= segStart && mid < segEnd) {
                if (eventCount < maxEvents) {
                    EventData event{};
                    event.targetNodeId = NodeID::invalid();
                    event.sampleOffset = static_cast<uint32_t>(mid - startPosition);
                    event.eventType = EventType::AUTOMATION;
                    event.payload.automation.parameterIndex = parameterIndex_;
                    event.payload.automation.targetValue = p_to.value;
                    event.payload.automation.rampDuration = 0;
                    outEvents[eventCount++] = event;
                }
            }
        } else {
            // Non-linear shapes: subdivide segment into smaller linear ramps
            constexpr uint64_t SUB_SEG_SIZE = 16;
            uint64_t pos = segStart;
            while (pos < segEnd) {
                uint64_t nextPos = std::min(pos + SUB_SEG_SIZE, segEnd);
                float targetVal = DSP::CurveInterpolator::calculate(p_from, p_to, nextPos);

                if (eventCount < maxEvents) {
                    EventData event{};
                    event.targetNodeId = NodeID::invalid();
                    event.sampleOffset = static_cast<uint32_t>(pos - startPosition);
                    event.eventType = EventType::AUTOMATION;
                    event.payload.automation.parameterIndex = parameterIndex_;
                    event.payload.automation.targetValue = targetVal;
                    event.payload.automation.rampDuration = static_cast<uint32_t>(nextPos - pos);
                    outEvents[eventCount++] = event;
                }
                pos = nextPos;
            }
        }
    }

    lastPosition_.store(endPosition, std::memory_order_relaxed);
    if (eventCount > 0) {
        lastValue_.store(outEvents[eventCount - 1].payload.automation.targetValue, std::memory_order_relaxed);
    }

    return eventCount;
}

void AutomationLane::updatePoints(const ::AutomationPoint* points, uint32_t count) {
    uint32_t inactiveIdx = 1 - activeBufferIndex_.load(std::memory_order_relaxed);
    uint32_t toCopy = std::min(count, MAX_PLAYBACK_POINTS);
    for (uint32_t i = 0; i < toCopy; ++i) {
        playbackPoints_[inactiveIdx][i] = points[i];
    }
    playbackPointsCount_[inactiveIdx] = toCopy;
    activeBufferIndex_.store(inactiveIdx, std::memory_order_release);
}

//==============================================================================
// AUTOMATION PROCESSOR IMPLEMENTATION
//==============================================================================

std::unique_ptr<IAutomationProcessor> IAutomationProcessor::create() {
    return std::make_unique<AutomationProcessorImpl>();
}

std::unique_ptr<IAutomationProcessor> AutomationProcessorImpl::create() {
    return std::make_unique<AutomationProcessorImpl>();
}

AutomationProcessorImpl::AutomationProcessorImpl()
    : laneCount_(0)
{
    // Pre-allocate all lanes to prevent runtime allocations on audio thread
    for (uint32_t i = 0; i < MAX_LANES; ++i) {
        lanes_[i].targetNodeId = NodeID::invalid();
        lanes_[i].subNodeId = 0;
        lanes_[i].parameterIndex = 0;
        lanes_[i].lane = std::make_unique<AutomationLane>(NodeID::invalid(), 0, 0);
    }
}

AutomationProcessorImpl::~AutomationProcessorImpl() = default;

uint32_t AutomationProcessorImpl::generateAutomationEvents(
    uint64_t startPosition,
    uint32_t numSamples,
    EventData* outEvents,
    uint32_t maxEvents,
    bool isPlaying
) {
    if (!outEvents || maxEvents == 0) {
        return 0;
    }

    uint32_t eventCount = 0;
    uint32_t currentCount = laneCount_.load(std::memory_order_acquire);

    for (uint32_t laneIdx = 0; laneIdx < currentCount; ++laneIdx) {
        if (lanes_[laneIdx].lane && lanes_[laneIdx].targetNodeId.isValid()) {
            uint32_t laneEvents = lanes_[laneIdx].lane->generateEvents(
                startPosition,
                numSamples,
                outEvents + eventCount,
                maxEvents - eventCount,
                isPlaying
            );

            for (uint32_t i = 0; i < laneEvents; ++i) {
                outEvents[eventCount + i].targetNodeId = lanes_[laneIdx].targetNodeId;
                outEvents[eventCount + i].payload.automation.targetSubNodeId = lanes_[laneIdx].subNodeId;
            }

            eventCount += laneEvents;

            if (eventCount >= maxEvents) {
                break;
            }
        }
    }

    return eventCount;
}

void AutomationProcessorImpl::recordAutomationValue(
    NodeID targetNodeId,
    uint32_t subNodeId,
    uint32_t parameterIndex,
    float value,
    uint64_t position
) {
    AutomationLane* lane = findOrCreateLane(targetNodeId, subNodeId, parameterIndex);
    if (!lane) {
        return;
    }
    lane->recordValue(value, position);
}

void AutomationProcessorImpl::updatePlaybackPoints(
    NodeID targetNodeId,
    uint32_t subNodeId,
    uint32_t parameterIndex,
    const ::AutomationPoint* points,
    uint32_t count
) {
    AutomationLane* lane = findOrCreateLane(targetNodeId, subNodeId, parameterIndex);
    if (lane) {
        lane->updatePoints(points, count);
    }
}

AutomationLane* AutomationProcessorImpl::findOrCreateLane(NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex) {
    uint32_t currentCount = laneCount_.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < currentCount; ++i) {
        if (lanes_[i].targetNodeId == targetNodeId && lanes_[i].subNodeId == subNodeId && lanes_[i].parameterIndex == parameterIndex && lanes_[i].lane) {
            return lanes_[i].lane.get();
        }
    }

    std::lock_guard<std::mutex> lock(laneMutex_);

    // Double-check
    currentCount = laneCount_.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < currentCount; ++i) {
        if (lanes_[i].targetNodeId == targetNodeId && lanes_[i].subNodeId == subNodeId && lanes_[i].parameterIndex == parameterIndex && lanes_[i].lane) {
            return lanes_[i].lane.get();
        }
    }

    if (currentCount >= MAX_LANES) {
        return nullptr;
    }

    // Configure the pre-allocated lane
    lanes_[currentCount].targetNodeId = targetNodeId;
    lanes_[currentCount].subNodeId = subNodeId;
    lanes_[currentCount].parameterIndex = parameterIndex;
    lanes_[currentCount].lane->setTarget(targetNodeId, subNodeId, parameterIndex);

    laneCount_.store(currentCount + 1, std::memory_order_release);

    return lanes_[currentCount].lane.get();
}

} // namespace Layer3
