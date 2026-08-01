#pragma once

#include "media_primitives.h"
#include <memory>
#include <vector>

#include "asset_blobs.h"

namespace MediaManagement {

/**
 * @brief Interface for the Media Registry.
 * 
 * The Media Registry is the central authority in Layer 6 for managing media assets.
 * It handles the creation of MediaID handles and the storage of AssetInfo metadata.
 */
class IMediaRegistry {
public:
    virtual ~IMediaRegistry() = default;

    /**
     * @brief Register a new media asset.
     * 
     * @param info Initial metadata for the asset.
     * @return A unique MediaID with an incremented generation counter.
     * @pre Called from Main Thread or worker threads.
     * @post Asset is stored and retrievable via getAssetInfo.
     */
    virtual MediaID registerAsset(const AssetInfo& info) = 0;

    /**
     * @brief Retrieve metadata for a specific MediaID.
     * 
     * @param id The MediaID to look up.
     * @param outInfo Reference to store the retrieved metadata.
     * @return true if the ID is valid and found, false otherwise.
     * @thread_safety Thread-safe (read-only access).
     */
    virtual bool getAssetInfo(MediaID id, AssetInfo& outInfo) const = 0;

    /**
     * @brief Update metadata for an existing asset.
     * 
     * Typically called after background analysis (BPM, Peak generation) completes.
     * 
     * @param id The MediaID to update.
     * @param info The new metadata to store.
     * @return true if the update was successful.
     */
    virtual bool updateAssetInfo(MediaID id, const AssetInfo& info) = 0;

    /**
     * @brief Blob Storage for large non-POD data.
     */
    virtual bool setAnalysisBlobs(MediaID id, const AssetAnalysisBlobs& blobs) = 0;
    virtual bool getAnalysisBlobs(MediaID id, AssetAnalysisBlobs& outBlobs) const = 0;

    virtual bool setWaveformBlobs(MediaID id, const AssetWaveformBlobs& blobs) = 0;
    virtual bool getWaveformBlobs(MediaID id, AssetWaveformBlobs& outBlobs) const = 0;

    /**
     * @brief Remove an asset from the registry.
     * 
     * @param id The MediaID to remove.
     * @return true if removed, false if not found.
     * @post The ID is invalidated (future generation checks will fail).
     */
    virtual bool removeAsset(MediaID id) = 0;

    /**
     * @brief Get the total number of registered assets.
     */
    virtual uint32_t getAssetCount() const = 0;

    /**
     * @brief Retrieve all registered MediaIDs.
     */
    virtual std::vector<MediaID> getAllMediaIDs() const = 0;

    /**
     * @brief Persistence
     */
    virtual bool serialize(const char* filePath) const = 0;
    virtual bool deserialize(const char* filePath) = 0;

    /**
     * @brief Factory method to create a concrete instance.
     */
    static std::unique_ptr<IMediaRegistry> create();
};

} // namespace MediaManagement
