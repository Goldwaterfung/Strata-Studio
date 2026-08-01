// aligned_allocator.cpp
// Layer 2: Core Infrastructure Services - SIMD-Aligned Memory Allocation

#include "aligned_allocator.h"

#ifdef HAVE_MLOCK
#include <sys/mman.h>
#elif defined(HAVE_VIRTUALLOCK)
#include <windows.h>
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#elif defined(__APPLE__)
#include <unistd.h>
#endif

namespace Layer2 {

void* AlignedAllocator::allocate(size_t size, size_t alignment)
{
#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
#endif
}

void AlignedAllocator::deallocate(void* ptr)
{
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

bool AlignedAllocator::lockMemory(void* ptr, size_t size)
{
#ifdef HAVE_MLOCK
    return mlock(ptr, size) == 0;
#elif defined(HAVE_VIRTUALLOCK)
    return VirtualLock(ptr, size) != 0;
#else
    (void)ptr;
    (void)size;
    return false;  // Not supported
#endif
}

bool AlignedAllocator::unlockMemory(void* ptr, size_t size)
{
#ifdef HAVE_MLOCK
    return munlock(ptr, size) == 0;
#elif defined(HAVE_VIRTUALLOCK)
    return VirtualUnlock(ptr, size) != 0;
#else
    (void)ptr;
    (void)size;
    return false;  // Not supported
#endif
}

} // namespace Layer2
