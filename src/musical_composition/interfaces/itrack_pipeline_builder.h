#pragma once
#include "musical_composition/track_manager/itrack_manager.h" // For TrackCreateInfo
#include "track_pipeline_descriptor.h"

// Forward declaration from Layer 3
namespace Layer3 { class IDSPKernel; }
using IDSPKernel = Layer3::IDSPKernel;

namespace composition {

/**
 * @brief Interface for building track processing pipelines
 * 
 * This interface is defined in Layer 5 and implemented in the Application layer
 * to avoid circular dependencies between Layer 5 and Layer 4 (DSP).
 */
class ITrackPipelineBuilder {
public:
    virtual ~ITrackPipelineBuilder() = default;

    /**
     * @brief Construct the DSP nodes required for a track type.
     * @param info Contains track type (AUDIO, MIDI, AUX, etc.)
     * @param kernel The Layer 3 DSP kernel where nodes will be registered.
     * @return A descriptor containing the created NodeIDs.
     */
    virtual TrackPipelineDescriptor buildPipeline(
        const TrackCreateInfo& info,
        IDSPKernel* kernel
    ) = 0;

    /**
     * @brief Tear down and unregister nodes when a track is deleted.
     */
    virtual void destroyPipeline(
        const TrackPipelineDescriptor& pipeline,
        IDSPKernel* kernel
    ) = 0;
};

} // namespace composition
