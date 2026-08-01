// aligned_allocator.h
// Layer 1: Hardware/OS Abstraction - Aligned Memory Allocator
// Ensures SIMD-compatible alignment (typically 32 or 64 bytes)

#pragma once

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>

namespace Layer1 {

/**
 * @brief STL-compatible allocator that ensures N-byte alignment
 * Useful for SIMD operations and avoiding false sharing.
 */
template <typename T, size_t Alignment = 64>
struct AlignedAllocator {
    using value_type = T;

    AlignedAllocator() noexcept = default;
    template <typename U> AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    T* allocate(size_t n) {
        if (n == 0) return nullptr;
        if (n > static_cast<size_t>(-1) / sizeof(T)) throw std::bad_array_new_length();

        void* ptr = nullptr;
#if defined(_MSC_VER) || defined(__MINGW32__)
        ptr = _aligned_malloc(n * sizeof(T), Alignment);
        if (!ptr) throw std::bad_alloc();
#else
        if (posix_memalign(&ptr, Alignment, n * sizeof(T)) != 0) throw std::bad_alloc();
#endif
        return static_cast<T*>(ptr);
    }

    void deallocate(T* p, size_t) noexcept {
#if defined(_MSC_VER) || defined(__MINGW32__)
        _aligned_free(p);
#else
        free(p);
#endif
    }

    template <typename U> struct rebind { using other = AlignedAllocator<U, Alignment>; };
    bool operator==(const AlignedAllocator&) const { return true; }
    bool operator!=(const AlignedAllocator&) const { return false; }
};

} // namespace Layer1
