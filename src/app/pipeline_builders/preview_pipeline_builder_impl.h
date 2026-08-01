#pragma once

#include <memory>

// Forward declarations from Layer 4 (DSP Nodes)
namespace dsp {
class IProcessorFactory;
}

#include "Media management/browser/ipreview_pipeline_builder.h"

namespace app {

/**
 * @brief Concrete implementation of IPreviewPipelineBuilder
 *
 * This class bridges Layer 6 (Media Management) with Layer 4 (DSP Nodes).
 * It implements the Layer 6 interface using Layer 4 factories, allowing
 * the media layer to create preview/solo audition DSP pipelines without
 * depending on Layer 4 directly.
 */
class PreviewPipelineBuilderImpl : public MediaManagement::IPreviewPipelineBuilder {
public:
    explicit PreviewPipelineBuilderImpl(
        std::shared_ptr<dsp::IProcessorFactory> processorFactory
    );

    ~PreviewPipelineBuilderImpl() override = default;

    // Implement IPreviewPipelineBuilder interface
    bool buildPreviewPipeline(const MediaManagement::DSPPreviewConfig& config) override;
    void destroyPreviewPipeline(MediaID mediaId) override;

private:
    std::shared_ptr<dsp::IProcessorFactory> m_processorFactory;
};

} // namespace app
