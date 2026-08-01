#pragma once
#include <cstdint>
#include "musical_composition/command_history/delta_primitives.h"
#include "common/system_primitives.h"


namespace composition {

// The specific operation identifiers for the Track Manager subsystem
namespace TrackOps {
    constexpr uint32_t CREATE_TRACK = 0;
    constexpr uint32_t DELETE_TRACK = 1;
    constexpr uint32_t MOVE_TRACK   = 2;
    constexpr uint32_t RENAME_TRACK = 3;
    constexpr uint32_t SET_COLOR    = 4;
    constexpr uint32_t SET_RECORD_ARMED = 5;
    constexpr uint32_t SET_INPUT_MONITORING = 6;
    constexpr uint32_t SET_TYPE = 7;
    constexpr uint32_t SET_LOCKED = 8;
    constexpr uint32_t SET_COMMENTS = 9;
    constexpr uint32_t SET_OUTPUT_ROUTING = 10;
}

// Payload structs that get memcpy'd into ProjectDelta's 256-byte buffer
struct MoveTrackPayload {
    uint32_t newIndexPosition;
    TrackID newParentFolderId;
};

struct RenameTrackPayload {
    uint32_t oldNameId;  // Layer 2 String Registry ID
    uint32_t newNameId;
};

static_assert(sizeof(MoveTrackPayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(RenameTrackPayload) <= 256, "Payload exceeds delta buffer");

struct SetTrackColorPayload {
    uint32_t oldColorARGB;
    uint32_t newColorARGB;
};

static_assert(sizeof(SetTrackColorPayload) <= 256, "Payload exceeds delta buffer");

struct SetRecordArmedPayload {
    bool oldArmed;
    bool newArmed;
};

struct SetInputMonitoringPayload {
    bool oldEnabled;
    bool newEnabled;
};

struct SetTrackTypePayload {
    TrackType oldType;
    TrackType newType;
};

static_assert(sizeof(SetRecordArmedPayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(SetInputMonitoringPayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(SetTrackTypePayload) <= 256, "Payload exceeds delta buffer");

struct SetTrackLockedPayload {
    bool oldLocked;
    bool newLocked;
};

static_assert(sizeof(SetTrackLockedPayload) <= 256, "Payload exceeds delta buffer");

struct SetTrackCommentsPayload {
    uint32_t oldCommentsId;
    uint32_t newCommentsId;
};

struct SetTrackOutputRoutingPayload {
    TrackID oldOutputTargetTrackId;
    TrackID newOutputTargetTrackId;
};

static_assert(sizeof(SetTrackCommentsPayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(SetTrackOutputRoutingPayload) <= 256, "Payload exceeds delta buffer");

} // namespace composition
