#pragma once
#include <cstdint>
#include "common/system_primitives.h"
#include "musical_composition/command_history/delta_primitives.h"

namespace composition {

namespace TempoTimelineOps {
    constexpr uint32_t SET_BPM            = 0;
    constexpr uint32_t ADD_TEMPO_POINT    = 1;
    constexpr uint32_t REMOVE_TEMPO_POINT = 2;
    constexpr uint32_t SET_TIME_SIGNATURE = 3;
    constexpr uint32_t SET_LOOP_RANGE     = 4;
    constexpr uint32_t SET_LOOP_ENABLED   = 5;
}

namespace MarkerOps {
    constexpr uint32_t ADD_MARKER         = 0;
    constexpr uint32_t REMOVE_MARKER      = 1;
    constexpr uint32_t UPDATE_MARKER      = 2;
}

struct SetBPMPayload {
    double oldBpm;
    double newBpm;
};

struct TempoPointPayload {
    uint64_t framePosition;
    double bpm;
};

struct TimeSignaturePayload {
    uint64_t framePosition;
    uint8_t oldNumerator;
    uint8_t oldDenominator;
    uint8_t newNumerator;
    uint8_t newDenominator;
    uint8_t reserved[4]; // Alignment padding
};

struct LoopRangePayload {
    uint64_t oldStartFrame;
    uint64_t oldEndFrame;
    uint64_t newStartFrame;
    uint64_t newEndFrame;
};

struct LoopEnabledPayload {
    bool oldEnabled;
    bool newEnabled;
    uint8_t reserved[6]; // Alignment padding
};

struct MarkerPayload {
    MarkerUUID uuid;
    uint64_t framePosition;
    char label[MAX_NAME_LENGTH]; // 64 bytes
    uint32_t colorARGB;
    uint32_t reserved; // Padding for 8-byte alignment
};

static_assert(sizeof(SetBPMPayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(TempoPointPayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(TimeSignaturePayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(LoopRangePayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(LoopEnabledPayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(MarkerPayload) <= 256, "Payload exceeds delta buffer");

} // namespace composition
