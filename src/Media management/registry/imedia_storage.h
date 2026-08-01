#pragma once

#include "asset_blobs.h"
#include <string>

namespace MediaManagement {

/**
 * @brief Interface for persistent storage of large media blobs.
 * 
 * Decouples the main project registry from the heavy analysis/waveform data.
 */
class IMediaStorage {
public:
    virtual ~IMediaStorage() = default;

    /**
     * @brief Save blobs for a specific asset.
     */
    virtual bool saveAnalysisBlobs(MediaID id, const AssetAnalysisBlobs& blobs) = 0;
    virtual bool loadAnalysisBlobs(MediaID id, AssetAnalysisBlobs& outBlobs) = 0;

    virtual bool saveWaveformBlobs(MediaID id, const AssetWaveformBlobs& blobs) = 0;
    virtual bool loadWaveformBlobs(MediaID id, AssetWaveformBlobs& outBlobs) = 0;

    /**
     * @brief Delete all data associated with an asset.
     */
    virtual bool deleteData(MediaID id) = 0;

    /**
     * @brief Configure the root storage path (e.g., project subfolder).
     */
    virtual void setRootPath(const std::string& path) = 0;
};

} // namespace MediaManagement
