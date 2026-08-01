#pragma once

#include "import_primitives.h"
#include <functional>
#include <memory>
#include <vector>

namespace Layer2 { class IStringRegistry; }

namespace MediaManagement {

class IMediaRegistry;
class ICodecFactory;
class IAudioAnalysisEngine;
class IWaveformRenderer;

/**
 * @brief Interface for the Project Browser service.
 * 
 * Handles asynchronous media imports, library queries, and background job management.
 * This is the primary interface used by Layer 7 (Presentation) for media handling.
 */
class IProjectBrowser {
public:
    virtual ~IProjectBrowser() = default;

    /**
     * @brief Signature for import completion callback.
     * Always invoked on the Main Thread.
     */
    using ImportCallback = void(*)(void* context,
                                   MediaID result,
                                   bool success,
                                   const char* errorMessage);

    /**
     * @brief Start an asynchronous media import.
     * 
     * @param job The import request descriptor.
     * @param callback Function to call when import is finished.
     * @param context User context pointer.
     * @return Unique jobId for tracking.
     */
    virtual uint64_t importAssetAsync(const ImportJob& job,
                                      ImportCallback callback,
                                      void* context) = 0;

    /**
     * @brief Poll for the progress of an ongoing job.
     */
    virtual bool getImportProgress(uint64_t jobId, float& outProgress, const char*& outErrorMessage) const = 0;

    /**
     * @brief Cancel an ongoing job.
     */
    virtual bool cancelImport(uint64_t jobId) = 0;

    /**
     * @brief Clean up completed or failed jobs from the tracking queue.
     */
    virtual void pruneJobs() = 0;

    /**
     * @brief Asset Query Operations
     */
    virtual uint32_t getAssetCount() const = 0;
    virtual bool getAssetInfo(MediaID mediaId, AssetInfo& outInfo) const = 0;
    virtual uint32_t findAssetsByName(uint32_t nameStringId, MediaID* outResults, uint32_t maxResults) const = 0;
    virtual bool removeAsset(MediaID mediaId) = 0;

    /**
     * @brief Service tick called from the Main Thread to process callbacks.
     */
    virtual void update() = 0;

    /**
     * @brief Factory method.
     */
    static std::unique_ptr<IProjectBrowser> create(IMediaRegistry* registry, 
                                                   Layer2::IStringRegistry* strings,
                                                   ICodecFactory* codecs,
                                                   IAudioAnalysisEngine* analysis,
                                                   IWaveformRenderer* waveforms);
};

} // namespace MediaManagement
