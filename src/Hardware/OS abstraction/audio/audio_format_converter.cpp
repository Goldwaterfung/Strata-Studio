// audio_format_converter.cpp
// Layer 1: Hardware/OS Abstraction - Audio Format Conversion Implementation
// SIMD intrinsics are quarantined to this file

#include "audio_format_converter.h"
#include <cstring>

// Platform-specific SIMD includes (quarantined to implementation)
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

namespace Layer1 {

// =============================================================================
// GENERIC CONVERSIONS (always available)
// =============================================================================

void AudioFormatConverter::interleavedToPlanar_Generic(
    const float* interleaved,
    float* const* planar,
    uint32_t numChannels,
    uint32_t numFrames)
{
    // De-interleave audio data
    // Input:  [ch0_frame0, ch1_frame0, ..., chN_frame0, ch0_frame1, ...]
    // Output: planar[0] = [ch0_frame0, ch0_frame1, ...]
    //         planar[1] = [ch1_frame0, ch1_frame1, ...]
    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            planar[ch][frame] = interleaved[frame * numChannels + ch];
        }
    }
}

void AudioFormatConverter::planarToInterleaved_Generic(
    const float* const* planar,
    float* interleaved,
    uint32_t numChannels,
    uint32_t numFrames)
{
    // Interleave audio data
    for (uint32_t frame = 0; frame < numFrames; ++frame) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            interleaved[frame * numChannels + ch] = planar[ch][frame];
        }
    }
}

// =============================================================================
// SIMD-OPTIMIZED CONVERSIONS (x86/x64 only)
// =============================================================================

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

void AudioFormatConverter::interleavedToPlanar_Stereo_SSE(
    const float* interleaved,
    float* left,
    float* right,
    uint32_t numFrames)
{
    constexpr uint32_t SIMD_WIDTH = 4;  // Process 4 stereo frames (8 floats) per iteration

    uint32_t i = 0;

    // SIMD loop: Process 4 frames at a time
    for (; i + SIMD_WIDTH <= numFrames; i += SIMD_WIDTH) {
        // Load 8 floats (4 stereo frames)
        __m128 interleaved0 = _mm_load_ps(&interleaved[i * 2]);      // L0, R0, L1, R1
        __m128 interleaved1 = _mm_load_ps(&interleaved[i * 2 + 4]);  // L2, R2, L3, R3

        // Unpack and shuffle to separate channels
        __m128 leftSamples = _mm_shuffle_ps(interleaved0, interleaved1,
                                            _MM_SHUFFLE(2, 0, 2, 0));  // L0, L1, L2, L3
        __m128 rightSamples = _mm_shuffle_ps(interleaved0, interleaved1,
                                             _MM_SHUFFLE(3, 1, 3, 1)); // R0, R1, R2, R3

        // Store planar outputs
        _mm_store_ps(&left[i], leftSamples);
        _mm_store_ps(&right[i], rightSamples);
    }

    // Scalar loop: Handle remaining frames
    for (; i < numFrames; ++i) {
        left[i] = interleaved[i * 2];
        right[i] = interleaved[i * 2 + 1];
    }
}

void AudioFormatConverter::planarToInterleaved_Stereo_SSE(
    const float* left,
    const float* right,
    float* interleaved,
    uint32_t numFrames)
{
    constexpr uint32_t SIMD_WIDTH = 4;  // Process 4 stereo frames (8 floats) per iteration

    uint32_t i = 0;

    // SIMD loop: Process 4 frames at a time
    for (; i + SIMD_WIDTH <= numFrames; i += SIMD_WIDTH) {
        // Load planar inputs
        __m128 leftSamples = _mm_load_ps(&left[i]);    // L0, L1, L2, L3
        __m128 rightSamples = _mm_load_ps(&right[i]);  // R0, R1, R2, R3

        // Interleave using unpack operations
        __m128 interleaved0 = _mm_unpacklo_ps(leftSamples, rightSamples);  // L0, R0, L1, R1
        __m128 interleaved1 = _mm_unpackhi_ps(leftSamples, rightSamples);  // L2, R2, L3, R3

        // Store interleaved output
        _mm_store_ps(&interleaved[i * 2], interleaved0);
        _mm_store_ps(&interleaved[i * 2 + 4], interleaved1);
    }

    // Scalar loop: Handle remaining frames
    for (; i < numFrames; ++i) {
        interleaved[i * 2] = left[i];
        interleaved[i * 2 + 1] = right[i];
    }
}

#endif // x86/x64 SIMD

// =============================================================================
// SIMD-OPTIMIZED CONVERSIONS (ARM NEON)
// =============================================================================

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>

void AudioFormatConverter::interleavedToPlanar_Stereo_NEON(
    const float* interleaved,
    float* left,
    float* right,
    uint32_t numFrames)
{
    constexpr uint32_t SIMD_WIDTH = 4;
    uint32_t i = 0;

    for (; i + SIMD_WIDTH <= numFrames; i += SIMD_WIDTH) {
        // Load 8 floats (4 stereo frames)
        float32x4x2_t interleaved_data = vld2q_f32(&interleaved[i * 2]);
        
        // Store de-interleaved data
        vst1q_f32(&left[i], interleaved_data.val[0]);
        vst1q_f32(&right[i], interleaved_data.val[1]);
    }

    for (; i < numFrames; ++i) {
        left[i] = interleaved[i * 2];
        right[i] = interleaved[i * 2 + 1];
    }
}

void AudioFormatConverter::planarToInterleaved_Stereo_NEON(
    const float* left,
    const float* right,
    float* interleaved,
    uint32_t numFrames)
{
    constexpr uint32_t SIMD_WIDTH = 4;
    uint32_t i = 0;

    for (; i + SIMD_WIDTH <= numFrames; i += SIMD_WIDTH) {
        float32x4x2_t interleaved_data;
        interleaved_data.val[0] = vld1q_f32(&left[i]);
        interleaved_data.val[1] = vld1q_f32(&right[i]);
        
        // Store interleaved data
        vst2q_f32(&interleaved[i * 2], interleaved_data);
    }

    for (; i < numFrames; ++i) {
        interleaved[i * 2] = left[i];
        interleaved[i * 2 + 1] = right[i];
    }
}
#endif

void AudioFormatConverter::interleavedToPlanar(
    const float* interleaved,
    float* const* planar,
    uint32_t numChannels,
    uint32_t numFrames)
{
    if (numChannels == 2) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        interleavedToPlanar_Stereo_SSE(interleaved, planar[0], planar[1], numFrames);
        return;
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
        interleavedToPlanar_Stereo_NEON(interleaved, planar[0], planar[1], numFrames);
        return;
#endif
    }
    interleavedToPlanar_Generic(interleaved, planar, numChannels, numFrames);
}

void AudioFormatConverter::planarToInterleaved(
    const float* const* planar,
    float* interleaved,
    uint32_t numChannels,
    uint32_t numFrames)
{
    if (numChannels == 2) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        planarToInterleaved_Stereo_SSE(planar[0], planar[1], interleaved, numFrames);
        return;
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
        planarToInterleaved_Stereo_NEON(planar[0], planar[1], interleaved, numFrames);
        return;
#endif
    }
    planarToInterleaved_Generic(planar, interleaved, numChannels, numFrames);
}

} // namespace Layer1

