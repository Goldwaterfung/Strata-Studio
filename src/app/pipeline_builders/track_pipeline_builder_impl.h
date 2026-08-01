#pragma once

#include <memory>

// Forward declarations from Layer 4 (DSP Nodes)
namespace dsp {
class IChannelStripFactory;
class IProcessorFactory;
}

#include "musical_composition/interfaces/itrack_pipeline_builder.h"

namespace app {

/**
 * @brief Concrete implementation of ITrackPipelineBuilder
 *
 * This class bridges Layer 5 (Musical Composition) with Layer 4 (DSP Nodes).
 * It implements the Layer 5 interface using Layer 4 factories, allowing
 * the composition layer to create DSP processing pipelines without
 * depending on Layer 4 directly.
 *
 * This is a key part of the Composition Root's dependency injection strategy:
 * - Layer 5 defines the ITrackPipelineBuilder interface
 * - Layer 4 provides the factories (IChannelStripFactory, etc.)
 * - The Application Root (this file) wires them together
 */
class TrackPipelineBuilderImpl : public composition::ITrackPipelineBuilder {
public:
    explicit TrackPipelineBuilderImpl(
        std::shared_ptr<dsp::IChannelStripFactory> channelStripFactory,
        std::shared_ptr<dsp::IProcessorFactory> processorFactory
    );

    explicit TrackPipelineBuilderImpl(
        std::unique_ptr<composition::ITrackPipelineBuilder> delegatingBuilder
    );

    ~TrackPipelineBuilderImpl() override = default;

    // Implement ITrackPipelineBuilder interface
    composition::TrackPipelineDescriptor buildPipeline(
        const composition::TrackCreateInfo& info,
        IDSPKernel* kernel
    ) override;
    
    void destroyPipeline(
        const composition::TrackPipelineDescriptor& pipeline,
        IDSPKernel* kernel
    ) override;

private:
    std::shared_ptr<dsp::IChannelStripFactory> m_channelStripFactory;
    std::shared_ptr<dsp::IProcessorFactory> m_processorFactory;
    std::unique_ptr<composition::ITrackPipelineBuilder> m_delegatingBuilder;
};

} // namespace app
