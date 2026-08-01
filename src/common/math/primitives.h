#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define DSP_ARCH_X86
#elif defined(__aarch64__) || defined(_M_ARM64)
#define DSP_ARCH_ARM64
#endif

namespace Math {

/**
 * @brief Mathematical constants for DSP.
 */
namespace Constants {
    constexpr float PI        = 3.14159265358979323846f;
    constexpr float TWO_PI    = 6.28318530717958647692f;
    constexpr float HALF_PI   = 1.57079632679489661923f;
    constexpr float INV_PI    = 0.31830988618379067154f;
    constexpr float SQRT2     = 1.41421356237309504880f;
    constexpr float INV_SQRT2 = 0.70710678118654752440f; // -3dB
    constexpr float EPSILON   = 1.192092896e-07f;        // Smallest positive float
    constexpr float DENORMAL_THRESHOLD = 1e-18f;        // Threshold for software flushing
}

/**
 * @brief Configures the Floating Point Unit (FPU) for real-time DSP.
 * 
 * Sets the Flush-to-Zero (FTZ) and Denormals-Are-Zero (DAZ) flags to 
 * ensure that the CPU does not stall when encountering extremely small
 * floating-point values. This is critical for maintaining real-time safety.
 */
inline void setupFPU() {
#if defined(DSP_ARCH_X86)
    // x86 SSE: Set FTZ (bit 15) and DAZ (bit 6) in the MXCSR register.
    unsigned int mxcsr = _mm_getcsr();
    mxcsr |= (1 << 15); // FTZ
    mxcsr |= (1 << 6);  // DAZ
    _mm_setcsr(mxcsr);
#elif defined(DSP_ARCH_ARM64)
    // ARM64: Set the Flush-to-Zero (FZ) bit (bit 24) in the FPCR register.
    uint64_t fpcr;
    __asm__ __volatile__(
        "mrs %[fpcr], fpcr           \n"
        "orr %[fpcr], %[fpcr], #(1 << 24) \n"
        "msr fpcr, %[fpcr]           \n"
        "isb                         \n"
        : [fpcr] "=r"(fpcr)
        : 
        : "memory"
    );
#endif
}

/**
 * @brief Checks if a floating point value is valid (not NaN or Inf).
 */
inline bool isValid(float value) {
    return std::isfinite(value);
}

/**
 * @brief Soft-sanitizes an audio sample.
 * 
 * Clamps NaNs and Infinities to zero. Also flushes values that are 
 * dangerously close to zero (denormals) to zero if the hardware 
 * flags are not sufficient.
 * 
 * @param sample The input audio sample.
 * @return The sanitized sample.
 */
inline float sanitize(float sample) {
    // Catch NaN and Infinity
    if (!std::isfinite(sample)) {
        return 0.0f;
    }
    
    // Catch Denormals (Software Flush)
    if (std::abs(sample) < Constants::DENORMAL_THRESHOLD) {
        return 0.0f;
    }
    
    return sample;
}

} // namespace Math
