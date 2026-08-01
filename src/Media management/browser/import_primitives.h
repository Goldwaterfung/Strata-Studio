#pragma once

#include "Media management/registry/media_primitives.h"
#include <cstdint>
#include <string>

namespace MediaManagement {

/**
 * @brief Current status of an ongoing import job.
 */
enum class ImportStatus : uint8_t {
    PENDING,
    DECODING,
    ANALYZING,
    GENERATING,
    COMPLETE,
    FAILED
};

/**
 * @brief Descriptor for an asynchronous import job (REQUEST ONLY).
 */
struct ImportJob {
    uint32_t filePathId;        ///< String handle for the source file path
    ImportOptions options;      ///< Configuration for this import
    uint64_t jobId;             ///< Unique job identifier (can be set by caller or service)
};

// POD check for ImportJob
static_assert(std::is_pod<ImportJob>::value, "ImportJob must be Plain Old Data");

} // namespace MediaManagement
