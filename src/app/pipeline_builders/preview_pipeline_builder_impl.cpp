#include "preview_pipeline_builder_impl.h"
#include <iostream>

namespace app {

PreviewPipelineBuilderImpl::PreviewPipelineBuilderImpl(
    std::shared_ptr<dsp::IProcessorFactory> processorFactory
)
    : m_processorFactory(std::move(processorFactory))
{
}

bool PreviewPipelineBuilderImpl::buildPreviewPipeline(const MediaManagement::DSPPreviewConfig& config) {
    // TODO: Implement preview pipeline building
    // 1. Create preview processors from factory
    // 2. Wire them for solo audition
    // 3. Register with preview ID

    std::cout << "PreviewPipelineBuilder: Building preview pipeline for media "
              << config.mediaId.id << std::endl;

    return true;
}

void PreviewPipelineBuilderImpl::destroyPreviewPipeline(MediaID mediaId) {
    // TODO: Implement preview pipeline destruction
    std::cout << "PreviewPipelineBuilder: Destroying preview pipeline "
              << mediaId.id << std::endl;
}

} // namespace app
