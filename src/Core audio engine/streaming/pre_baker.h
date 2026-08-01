// src/Core audio engine/streaming/pre_baker.h
#pragma once

#include "Core audio engine/scheduler/offline_dsp_thread_pool.h"
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace Layer3 {

class PreBaker {
public:
    // Initialize with a reference to the offline DSP thread pool.
    explicit PreBaker(OfflineDSPThreadPool* threadPool);
    ~PreBaker();

    // Set the current project directory to determine where the cache is located.
    // If empty or unsaved, defaults to "~/DAW/Untitled Project/.daw_cache/bakes/"
    void setProjectDirectory(const std::string& projectDir);

private:
    std::string generateCachePath(uint32_t clipId, double targetRatio) const;

    std::string projectDir_;
    std::string cacheDir_;
    mutable std::mutex cacheMutex_;
};

} // namespace Layer3
