#pragma once

#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Layer1 {

/**
 * @brief RAII handler to set CPU flags for Flush-To-Zero (FTZ) and 
 * Denormals-Are-Zero (DAZ) on the current thread.
 * 
 * This is essential for audio threads to prevent performance degradation 
 * when processing extremely small floating point values.
 */
struct ScopedDenormalHandler {
    ScopedDenormalHandler() {
#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || defined(_M_IX86)
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__arm64__) || defined(__aarch64__)
        uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        fpcr |= (1ULL << 24); // FZ bit
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
    }
};

} // namespace Layer1

