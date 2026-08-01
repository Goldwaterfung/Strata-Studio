#pragma once

#include "imedia_storage.h"
#include <mutex>

namespace MediaManagement {

class SidecarMediaStorage : public IMediaStorage {
public:
    SidecarMediaStorage() = default;
    
    bool saveAnalysisBlobs(MediaID id, const AssetAnalysisBlobs& blobs) override;
    bool loadAnalysisBlobs(MediaID id, AssetAnalysisBlobs& outBlobs) override;

    bool saveWaveformBlobs(MediaID id, const AssetWaveformBlobs& blobs) override;
    bool loadWaveformBlobs(MediaID id, AssetWaveformBlobs& outBlobs) override;

    bool deleteData(MediaID id) override;
    void setRootPath(const std::string& path) override;

private:
    std::string getAnalysisPath(MediaID id) const;
    std::string getWaveformPathStr(MediaID id) const;

    std::string rootPath_;
    mutable std::mutex mutex_;
};

} // namespace MediaManagement
