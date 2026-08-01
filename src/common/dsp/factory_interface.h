#pragma once

#include <cstdint>
#include "../system_primitives.h"

namespace DSP {

/**
 * @brief Base interface for all DSP node factories.
 * 
 * Factories are responsible for the lifecycle of specific node types
 * (e.g., ChannelStrip, Panner) and manage their internal StateRegistries.
 */
class IDSPNodeFactory {
public:
    virtual ~IDSPNodeFactory() = default;

    /**
     * @brief Creates a new instance of the node.
     * @return NodeID The unique handle for the new node, or invalid() if allocation failed.
     */
    virtual NodeID createNode() = 0;

    /**
     * @brief Destroys a node instance by its ID.
     */
    virtual void destroyNode(NodeID nodeId) = 0;

    /**
     * @brief Returns the unique type identifier for the nodes created by this factory.
     */
    virtual uint32_t getNodeType() const = 0;

    /**
     * @brief Returns the current latency of a specific node in samples.
     */
    virtual uint32_t getLatency(NodeID nodeId) const = 0;
};

} // namespace DSP

