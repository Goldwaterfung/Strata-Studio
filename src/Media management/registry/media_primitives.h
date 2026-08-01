#pragma once

#include "common/system_primitives.h"
#include <type_traits>

namespace MediaManagement {

/**
 * @brief Detailed metadata for a media asset (TRUE POD).
 * 
 * This structure is owned by Layer 6's MediaRegistry and represents the
 * "source of truth" for any audio file imported into the project.
 * Layer 5 (Musical Composition) refers to these assets via the opaque MediaID.
 */
struct AssetInfo {
    MediaID mediaId;            ///< Unique identifier with generation counter
    uint32_t nameId;            ///< String handle for filename (Layer 2 String Registry)
    uint32_t pathId;            ///< String handle for full path (Layer 2 String Registry)
    
    uint64_t sizeBytes;         ///< File size in bytes
    uint64_t durationSamples;   ///< Total number of audio samples (duration)
    uint32_t sampleRate;        ///< Sampling rate in Hz
    uint16_t numChannels;       ///< Number of channels (1=Mono, 2=Stereo, etc.)
    uint16_t bitDepth;          ///< Source bit depth (e.g. 16, 24, 32)
    
    float peakDecibels;         ///< Absolute peak level in dBFS
    float rmsDecibels;          ///< Average RMS level in dBFS
    
    uint64_t importTime;        ///< Timestamp of import
    uint32_t colorARGB;         ///< Associated UI color
    
    struct {
        bool analyzed;          ///< Flag indicating if analysis is complete
        float tempo;            ///< Detected BPM
        float tempoConfidence;  ///< Confidence (0.0 - 1.0)
        uint8_t keyRoot;        ///< Root note (0=C, 1=C#, etc.)
        bool isMinor;           ///< True if minor, false if major
        float keyConfidence;    ///< Confidence (0.0 - 1.0)
        float integratedLUFS;   ///< Loudness
        float loudnessRange;    ///< Dynamic range
    } analysis;
    
    uint64_t fileModTime;       ///< File modification time
    uint32_t checksum;          ///< Content checksum
    uint8_t reserved[4];        ///< Padding
};

// COMPILE-TIME ASSERTIONS: AssetInfo must be POD
static_assert(std::is_pod<AssetInfo>::value, "AssetInfo must be Plain Old Data");
static_assert(sizeof(AssetInfo) == 104, "AssetInfo must have deterministic layout");

/**
 * @brief Options for media import jobs.
 */
struct ImportOptions {
    bool convertToMono;         ///< Downmix to mono if stereo
    bool normalize;             ///< Normalize on import
    float targetDecibels;       ///< Normalization target
    bool detectTransients;      ///< Run transient detection
    bool generateWaveform;      ///< Generate multi-res peaks immediately
    bool analyzeAudio;          ///< Perform Tier 2 analysis (loudness, spectral)
    bool detectTempo;           ///< Perform Tier 3 analysis (BPM)
    bool detectKey;             ///< Perform Key detection
    int32_t transpose;          ///< Semitone shift on import
    
    static constexpr ImportOptions defaults() {
        return { false, false, -0.1f, true, true, true, true, true, 0 };
    }
};

static_assert(std::is_pod<ImportOptions>::value, "ImportOptions must be Plain Old Data");

} // namespace MediaManagement
