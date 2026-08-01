#pragma once

#include "common/system_primitives.h"

namespace MediaManagement {

/**
 * @brief Configuration for DSP preview pipeline construction
 */
struct DSPPreviewConfig {
    MediaID mediaId;
    float startPosition;
    float duration;
    bool loop;
};

/**
 * @brief Interface for building preview processing pipelines
 * 
 * Note: This interface is currently reserved for future integration 
 * with ISampleLibraryBrowser.
 */
class IPreviewPipelineBuilder {
public:
    virtual ~IPreviewPipelineBuilder() = default;

    /**
     * @brief Build a processing pipeline for media preview
     * @param config The preview configuration
     * @return true if successful
     */
    virtual bool buildPreviewPipeline(const DSPPreviewConfig& config) = 0;

    /**
     * @brief Destroy a preview processing pipeline
     * @param mediaId The ID of the media being previewed
     */
    virtual void destroyPreviewPipeline(MediaID mediaId) = 0;
};

} // namespace MediaManagement
