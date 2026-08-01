// src/Core audio engine/sidechain/isidechain_manager.h
#pragma once

#include "system_primitives.h"
#include <cstdint>
#include <memory>

namespace Layer3 {

//==============================================================================
// SIDECHAIN MANAGER INTERFACE
//==============================================================================

class ISidechainManager {
public:
    //==========================================================================
    // Factory
    //==========================================================================

    static std::unique_ptr<ISidechainManager> create();

    //==========================================================================
    // Sidechain Registration (Non-RT-Safe)
    //==========================================================================

    // Register sidechain input
    // Parameters:
    //   nodeId: Destination node ID
    //   inputIndex: Input index on destination node
    // Thread-safety: NOT RT-safe (allocates buffer)
    virtual void registerSidechainInput(NodeID nodeId, uint32_t inputIndex) = 0;

    // Unregister sidechain input
    // Parameters:
    //   nodeId: Destination node ID
    //   inputIndex: Input index on destination node
    // Thread-safety: NOT RT-safe
    virtual void unregisterSidechainInput(NodeID nodeId, uint32_t inputIndex) = 0;

    //==========================================================================
    // Buffer Access (RT-Safe)
    //==========================================================================

    // Get primary channel (channel 0) sidechain buffer for node
    // Parameters:
    //   nodeId: Destination node ID
    //   inputIndex: Input index on destination node
    // Returns: Pointer to channel 0 sidechain buffer, nullptr if not registered
    // Thread-safety: RT-safe, wait-free
    virtual float* getSidechainBuffer(NodeID nodeId, uint32_t inputIndex) = 0;

    // Get planar multi-channel sidechain buffer struct for node
    // Thread-safety: RT-safe, wait-free
    virtual PlanarSidechainBuffer getSidechainPlanarBuffer(NodeID nodeId, uint32_t inputIndex) = 0;

    //==========================================================================
    // Cycle Operations (RT-Safe, Wait-Free)
    //==========================================================================

    // Zero out all active sidechain buffers prior to process cycle
    virtual void clearAllBuffers(uint32_t numFrames) = 0;

    // Accumulate input audio into a destination sidechain buffer
    virtual void accumulateSidechainInput(NodeID destNodeId, uint32_t inputIndex, 
                                          float* const* inputs, uint32_t numChannels, 
                                          uint32_t numFrames, float gain) = 0;

    //==========================================================================
    // Destructor
    //==========================================================================

    virtual ~ISidechainManager() = default;
};

} // namespace Layer3
