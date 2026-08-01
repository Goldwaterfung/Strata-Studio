// file_buffer_pool.h
// Layer 1: Hardware/OS Abstraction - File Buffer Pool

#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <memory>
#include "filesystem_defaults.h"

namespace Layer1 {

class FileBufferPool {
public:
    static FileBufferPool& getInstance() {
        static FileBufferPool instance;
        return instance;
    }

    uint8_t* acquire(size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        if (size == standardBufferSize && !pool.empty()) {
            uint8_t* ptr = pool.back();
            pool.pop_back();
            return ptr;
        }
        return new uint8_t[size];
    }

    void release(uint8_t* ptr, size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        if (size == standardBufferSize) {
            pool.push_back(ptr);
        } else {
            delete[] ptr;
        }
    }

private:
    FileBufferPool() = default;
    size_t standardBufferSize = FilesystemDefaults::STANDARD_BUFFER_SIZE;
    std::vector<uint8_t*> pool;
    std::mutex mutex;
};

} // namespace Layer1
