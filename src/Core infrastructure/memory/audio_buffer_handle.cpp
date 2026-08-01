// audio_buffer_handle.cpp
// Layer 2: Core Infrastructure Services - Audio Buffer RAII Handle

#include "imemory_coordinator.h"

using namespace Layer2;

AudioBufferHandle::AudioBufferHandle()
    : pool(nullptr)
    , buffer(nullptr)
{
}

AudioBufferHandle::AudioBufferHandle(IMemoryCoordinator* p, AudioBuffer* buf)
    : pool(p)
    , buffer(buf)
{
}

AudioBufferHandle::~AudioBufferHandle()
{
    if (pool && buffer) {
        pool->releaseBuffer(*buffer);
    }
}

AudioBufferHandle::AudioBufferHandle(AudioBufferHandle&& other) noexcept
    : pool(other.pool)
    , buffer(other.buffer)
{
    other.pool = nullptr;
    other.buffer = nullptr;
}

AudioBufferHandle& AudioBufferHandle::operator=(AudioBufferHandle&& other) noexcept
{
    if (this != &other) {
        // Release current buffer if we own one
        if (pool && buffer) {
            pool->releaseBuffer(*buffer);
        }

        // Take ownership of other's buffer
        pool = other.pool;
        buffer = other.buffer;

        // Clear other
        other.pool = nullptr;
        other.buffer = nullptr;
    }
    return *this;
}

AudioBuffer& AudioBufferHandle::get()
{
    return *buffer;
}

const AudioBuffer& AudioBufferHandle::get() const
{
    return *buffer;
}

AudioBuffer* AudioBufferHandle::operator->()
{
    return buffer;
}

const AudioBuffer* AudioBufferHandle::operator->() const
{
    return buffer;
}

AudioBufferHandle::operator bool() const
{
    return buffer != nullptr && pool != nullptr;
}

bool AudioBufferHandle::isValid() const
{
    return buffer != nullptr && pool != nullptr;
}
