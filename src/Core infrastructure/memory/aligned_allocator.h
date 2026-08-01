// aligned_allocator.h
// Layer 2: Core Infrastructure Services - SIMD-Aligned Memory Allocation
// Platform-specific memory allocation with alignment and locking support

#pragma once

#include <cstdint>
#include <cstdlib>

namespace Layer2 {

// SIMD-aligned memory allocation
// Thread-safety: Not thread-safe, caller must synchronize
class AlignedAllocator {
public:
    // Allocate aligned memory (64-byte for cache line alignment)
    // Thread-safety: Not thread-safe
    static void* allocate(size_t size, size_t alignment = 64);

    // Free aligned memory
    // Thread-safety: Not thread-safe
    static void deallocate(void* ptr);

    // Lock memory to prevent paging (platform-specific)
    // Returns: true if memory was locked successfully
    // Thread-safety: Not thread-safe
    static bool lockMemory(void* ptr, size_t size);

    static bool unlockMemory(void* ptr, size_t size);

};

} // namespace Layer2
