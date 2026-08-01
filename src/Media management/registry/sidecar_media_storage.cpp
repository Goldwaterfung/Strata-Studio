#include "sidecar_media_storage.h"
#include <cstdio>
#include <vector>

namespace MediaManagement {

void SidecarMediaStorage::setRootPath(const std::string& path) {
    std::unique_lock lock(mutex_);
    rootPath_ = path;
    // Note: In a real app, we'd ensure the directory exists here.
}

std::string SidecarMediaStorage::getAnalysisPath(MediaID id) const {
    return rootPath_ + "/asset_" + std::to_string(id.id) + "_" + std::to_string(id.generation) + ".analysis";
}

std::string SidecarMediaStorage::getWaveformPathStr(MediaID id) const {
    return rootPath_ + "/asset_" + std::to_string(id.id) + "_" + std::to_string(id.generation) + ".waveform";
}

bool SidecarMediaStorage::saveAnalysisBlobs(MediaID id, const AssetAnalysisBlobs& blobs) {
    std::unique_lock lock(mutex_);
    FILE* file = std::fopen(getAnalysisPath(id).c_str(), "wb");
    if (!file) return false;

    auto writeVector = [&](const auto& vec) {
        uint32_t size = static_cast<uint32_t>(vec.size());
        std::fwrite(&size, 4, 1, file);
        if (size > 0) std::fwrite(vec.data(), sizeof(typename std::decay_t<decltype(vec)>::value_type), size, file);
    };

    writeVector(blobs.spectralFlux);
    writeVector(blobs.transientPositions);
    writeVector(blobs.transientAmplitudes);
    writeVector(blobs.pitchData);

    std::fclose(file);
    return true;
}

bool SidecarMediaStorage::loadAnalysisBlobs(MediaID id, AssetAnalysisBlobs& outBlobs) {
    std::unique_lock lock(mutex_);
    FILE* file = std::fopen(getAnalysisPath(id).c_str(), "rb");
    if (!file) return false;

    auto readVector = [&](auto& vec) {
        uint32_t size = 0;
        if (std::fread(&size, 4, 1, file) == 1 && size > 0) {
            vec.resize(size);
            std::fread(vec.data(), sizeof(typename std::decay_t<decltype(vec)>::value_type), size, file);
        }
    };

    readVector(outBlobs.spectralFlux);
    readVector(outBlobs.transientPositions);
    readVector(outBlobs.transientAmplitudes);
    readVector(outBlobs.pitchData);

    std::fclose(file);
    return true;
}

bool SidecarMediaStorage::saveWaveformBlobs(MediaID id, const AssetWaveformBlobs& blobs) {
    std::unique_lock lock(mutex_);
    FILE* file = std::fopen(getWaveformPathStr(id).c_str(), "wb");
    if (!file) return false;

    auto writeVector = [&](const auto& vec) {
        uint32_t size = static_cast<uint32_t>(vec.size());
        std::fwrite(&size, 4, 1, file);
        if (size > 0) std::fwrite(vec.data(), sizeof(typename std::decay_t<decltype(vec)>::value_type), size, file);
    };

    uint32_t count = static_cast<uint32_t>(blobs.peaks.size());
    std::fwrite(&count, 4, 1, file);
    for (const auto& [res, p] : blobs.peaks) {
        std::fwrite(&res, 1, 1, file);
        writeVector(p);
        
        auto it = blobs.rms.find(res);
        if (it != blobs.rms.end()) writeVector(it->second);
        else { uint32_t zero = 0; std::fwrite(&zero, 4, 1, file); }
    }

    std::fclose(file);
    return true;
}

bool SidecarMediaStorage::loadWaveformBlobs(MediaID id, AssetWaveformBlobs& outBlobs) {
    std::unique_lock lock(mutex_);
    FILE* file = std::fopen(getWaveformPathStr(id).c_str(), "rb");
    if (!file) return false;

    auto readVector = [&](auto& vec) {
        uint32_t size = 0;
        if (std::fread(&size, 4, 1, file) == 1 && size > 0) {
            vec.resize(size);
            std::fread(vec.data(), sizeof(typename std::decay_t<decltype(vec)>::value_type), size, file);
        }
    };

    uint32_t count = 0;
    if (std::fread(&count, 4, 1, file) == 1) {
        for (uint32_t i = 0; i < count; ++i) {
            WaveformResolution res;
            std::fread(&res, 1, 1, file);
            readVector(outBlobs.peaks[res]);
            readVector(outBlobs.rms[res]);
        }
    }

    std::fclose(file);
    return true;
}

bool SidecarMediaStorage::deleteData(MediaID id) {
    std::unique_lock lock(mutex_);
    std::remove(getAnalysisPath(id).c_str());
    std::remove(getWaveformPathStr(id).c_str());
    return true;
}

} // namespace MediaManagement
