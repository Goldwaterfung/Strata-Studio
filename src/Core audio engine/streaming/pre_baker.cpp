// src/Core audio engine/streaming/pre_baker.cpp
#include "pre_baker.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <cstdlib>

namespace Layer3 {

PreBaker::PreBaker(OfflineDSPThreadPool* threadPool)
{
    (void)threadPool;
    // Set default project directory
    setProjectDirectory("");
}

PreBaker::~PreBaker() = default;

void PreBaker::setProjectDirectory(const std::string& projectDir) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    if (projectDir.empty()) {
        const char* homeDir = std::getenv("HOME");
        std::string base = homeDir ? homeDir : "";
        projectDir_ = base + "/DAW/Untitled Project";
    } else {
        projectDir_ = projectDir;
    }
    
    cacheDir_ = projectDir_ + "/.daw_cache/bakes/";
    
    // In a real application, ensure the directory exists here via std::filesystem
    // e.g. std::filesystem::create_directories(cacheDir_);
}

std::string PreBaker::generateCachePath(uint32_t clipId, double targetRatio) const {
    std::ostringstream oss;
    oss << cacheDir_ << "bake_" << clipId << "_ratio_" 
        << std::fixed << std::setprecision(4) << targetRatio << ".raw";
    return oss.str();
}

} // namespace Layer3
