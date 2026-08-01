#pragma once
#include <cstdint>
#include <cstring>
#include "musical_composition/project_session/iproject_session.h"

namespace composition {

namespace ProjectMetadataOps {
    constexpr uint32_t SET_METADATA = 0;
}

struct ProjectMetadataPayload {
    char projectName[128];
    char author[64];
    uint32_t sampleRate;
    float initialTempoBPM;
    uint8_t timeSignatureNumerator;
    uint8_t timeSignatureDenominator;
    uint8_t reserved_perf;
    uint32_t targetBitDepth;          // NEW
    double sessionDurationSeconds;    // NEW
};

static_assert(sizeof(ProjectMetadataPayload) <= 256, "ProjectMetadataPayload exceeds delta buffer");

inline ProjectMetadataPayload toPayload(const ProjectMetadata& meta) {
    ProjectMetadataPayload payload{};
    std::strncpy(payload.projectName, meta.projectName.c_str(), sizeof(payload.projectName) - 1);
    std::strncpy(payload.author, meta.author.c_str(), sizeof(payload.author) - 1);
    payload.sampleRate = meta.sampleRate;
    payload.initialTempoBPM = meta.initialTempoBPM;
    payload.timeSignatureNumerator = meta.timeSignatureNumerator;
    payload.timeSignatureDenominator = meta.timeSignatureDenominator;
    payload.reserved_perf = 0;
    payload.targetBitDepth = meta.targetBitDepth;
    payload.sessionDurationSeconds = meta.sessionDurationSeconds;
    return payload;
}

inline ProjectMetadata fromPayload(const ProjectMetadataPayload& payload) {
    ProjectMetadata meta{};
    meta.projectName = payload.projectName;
    meta.author = payload.author;
    meta.sampleRate = payload.sampleRate;
    meta.initialTempoBPM = payload.initialTempoBPM;
    meta.timeSignatureNumerator = payload.timeSignatureNumerator;
    meta.timeSignatureDenominator = payload.timeSignatureDenominator;
    meta.targetBitDepth = payload.targetBitDepth;
    meta.sessionDurationSeconds = payload.sessionDurationSeconds;
    return meta;
}

} // namespace composition
