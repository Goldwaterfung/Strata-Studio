#include "track_pipeline_builder_impl.h"
#include <iostream>

namespace app {

TrackPipelineBuilderImpl::TrackPipelineBuilderImpl(
    std::shared_ptr<dsp::IChannelStripFactory> channelStripFactory,
    std::shared_ptr<dsp::IProcessorFactory> processorFactory
)
    : m_channelStripFactory(std::move(channelStripFactory))
    , m_processorFactory(std::move(processorFactory))
{
}

TrackPipelineBuilderImpl::TrackPipelineBuilderImpl(
    std::unique_ptr<composition::ITrackPipelineBuilder> delegatingBuilder
)
    : m_delegatingBuilder(std::move(delegatingBuilder))
{
}

composition::TrackPipelineDescriptor TrackPipelineBuilderImpl::buildPipeline(
    const composition::TrackCreateInfo& info,
    IDSPKernel* kernel
) {
    std::cout << "TrackPipelineBuilder: Building pipeline for track" << std::endl;

    if (m_delegatingBuilder) {
        return m_delegatingBuilder->buildPipeline(info, kernel);
    }

    return composition::TrackPipelineDescriptor{};
}

void TrackPipelineBuilderImpl::destroyPipeline(
    const composition::TrackPipelineDescriptor& pipeline,
    IDSPKernel* kernel
) {
    std::cout << "TrackPipelineBuilder: Destroying pipeline" << std::endl;

    if (m_delegatingBuilder) {
        m_delegatingBuilder->destroyPipeline(pipeline, kernel);
        return;
    }
}

} // namespace app
