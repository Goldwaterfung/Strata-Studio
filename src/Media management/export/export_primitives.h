#pragma once

#include "common/system_primitives.h"
#include <cstdint>
#include <string>

namespace MediaManagement {

/**
 * @brief Supported export file formats.
 */
enum class ExportFormat : uint8_t {
    WAV,
    AIFF,
    FLAC,
    MP3,
    OGG
};

/**
 * @brief Supported export bit depths.
 */
enum class ExportBitDepth : uint8_t {
    BIT_16,
    BIT_24,
    BIT_32_FLOAT
};

/**
 * @brief Dithering algorithms.
 */
enum class DitherType : uint8_t {
    NONE,
    TPDF,           ///< Triangular Probability Density Function
    NOISE_SHAPING   ///< Psychoacoustic noise shaping
};

/**
 * @brief Configuration for an export job.
 */
struct ExportConfig {
    uint32_t outputPathId;      ///< String handle from Layer 2 String Registry
    uint64_t startSample;
    uint64_t endSample;
    uint32_t sampleRate;
    uint16_t numChannels;
    
    ExportFormat format;
    ExportBitDepth bitDepth;
    
    bool normalize;
    float normalizationdB;      ///< Target peak level in dBFS (e.g. -0.1f)
    
    DitherType dither;
    
    // Stem Export Support
    bool stemExport;
    uint32_t numStemNodes;
    NodeID stemNodes[32];       ///< Nodes (tracks/buses) to export as individual stems
    
    // Metadata (String handles from Layer 2)
    uint32_t titleId;
    uint32_t artistId;
    uint32_t albumId;
    uint32_t genreId;
    uint32_t commentId;

    // Track Isolation
    uint32_t isolateTrackId;
};

static_assert(std::is_pod<ExportConfig>::value, "ExportConfig must be Plain Old Data");

/**
 * @brief Status of an ongoing export job.
 */
enum class ExportStatus : uint8_t {
    PENDING,
    PREPARING,
    PROCESSING,
    FINALIZING,
    COMPLETED,
    FAILED,
    CANCELLED
};

struct ExportProgress {
    uint64_t jobId;
    ExportStatus status;
    float progress;             ///< 0.0 to 1.0
    char errorMessage[128];
};

static_assert(std::is_pod<ExportProgress>::value, "ExportProgress must be Plain Old Data");

} // namespace MediaManagement
