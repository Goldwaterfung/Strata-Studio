// audio_format_converter.h
// Layer 1: Hardware/OS Abstraction - Audio Format Conversion
// High-performance interleaved <-> planar conversion with SIMD optimization
// PURE HEADER: No platform-specific SIMD intrinsics

#pragma once

#include <cstdint>

namespace Layer1 {

// =============================================================================
// AUDIO FORMAT CONVERTER
// =============================================================================

class AudioFormatConverter {
public:
    // Generic interleaved to planar conversion (N channels)
    // This is always available and works for any channel count
    static void interleavedToPlanar_Generic(
        const float* interleaved,
        float* const* planar,     // Array of channel pointers
        uint32_t numChannels,
        uint32_t numFrames);

    // Generic planar to interleaved conversion (N channels)
    static void planarToInterleaved_Generic(
        const float* const* planar,     // Array of channel pointers
        float* interleaved,
        uint32_t numChannels,
        uint32_t numFrames);

    // High-level dispatchers (automatic SIMD)
    static void interleavedToPlanar(
        const float* interleaved,
        float* const* planar,
        uint32_t numChannels,
        uint32_t numFrames);

    static void planarToInterleaved(
        const float* const* planar,
        float* interleaved,
        uint32_t numChannels,
        uint32_t numFrames);

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    // SSE-optimized stereo conversions (available on x86/x64)
    static void interleavedToPlanar_Stereo_SSE(
        const float* interleaved,   // Input: interleaved stereo
        float* left,                // Output: left channel
        float* right,               // Output: right channel
        uint32_t numFrames);

    static void planarToInterleaved_Stereo_SSE(
        const float* left,
        const float* right,
        float* interleaved,
        uint32_t numFrames);
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    // NEON-optimized stereo conversions (available on ARM/ARM64)
    static void interleavedToPlanar_Stereo_NEON(
        const float* interleaved,
        float* left,
        float* right,
        uint32_t numFrames);

    static void planarToInterleaved_Stereo_NEON(
        const float* left,
        const float* right,
        float* interleaved,
        uint32_t numFrames);
#endif
};

} // namespace Layer1
