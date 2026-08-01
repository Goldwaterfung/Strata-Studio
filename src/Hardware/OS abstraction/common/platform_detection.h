// platform_detection.h
// Layer 1: Hardware/OS Abstraction - Platform Detection Macros

#pragma once

// Operating System Detection
#if defined(_WIN32) || defined(_WIN64)
    #define LAYER1_PLATFORM_WINDOWS 1
#elif defined(__APPLE__) && defined(__MACH__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC
        #define LAYER1_PLATFORM_MACOS 1
    #else
        #error "Unsupported Apple platform"
    #endif
#else
    #error "Unknown or unsupported platform"
#endif

// CPU Architecture Detection
#if defined(__x86_64__) || defined(_M_X64)
    #define LAYER1_ARCH_X64 1
#elif defined(__i386__) || defined(_M_IX86)
    #define LAYER1_ARCH_X86 1
#elif defined(__arm64__) || defined(__aarch64__) || defined(_M_ARM64)
    #define LAYER1_ARCH_ARM64 1
#elif defined(__arm__) || defined(_M_ARM)
    #define LAYER1_ARCH_ARM 1
#else
    #define LAYER1_ARCH_UNKNOWN 1
#endif

// SIMD Support Detection
#if defined(LAYER1_ARCH_X64) || defined(LAYER1_ARCH_X86)
    #define LAYER1_USE_SSE 1
    // AVX2 detection can be added here if needed
#elif defined(LAYER1_ARCH_ARM64) || defined(LAYER1_ARCH_ARM)
    #if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(LAYER1_ARCH_ARM64)
        #define LAYER1_USE_NEON 1
    #endif
#endif
