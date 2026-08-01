#pragma once

#include "state_registry.h"
#include "factory_interface.h"

namespace DSP {

/**
 * @brief Base class for node factories to reduce boilerplate.
 * 
 * Manages the association between a Factory and its StateRegistry.
 * 
 * @tparam StateT The POD state structure for the node.
 * @tparam MaxNodes Maximum number of instances.
 * @tparam NodeType The unique uint32_t type identifier for this node.
 */
template <typename StateT, uint32_t MaxNodes, uint32_t NodeType>
class BaseNodeFactory : public IDSPNodeFactory {
public:
    static StateRegistry<StateT, MaxNodes>& getRegistry() {
        static StateRegistry<StateT, MaxNodes> registry;
        return registry;
    }

    NodeID createNode() override {
        auto id = getRegistry().allocate();
        return id.has_value() ? *id : NodeID::invalid();
    }

    void destroyNode(NodeID nodeId) override {
        getRegistry().deallocate(nodeId);
    }

    uint32_t getNodeType() const override {
        return NodeType;
    }

    uint32_t getLatency(NodeID /*nodeId*/) const override {
        return 0; // Default implementation
    }
};

/**
 * @brief Helper macro to validate a NodeID and retrieve the state in the audio loop.
 * 
 * Usage:
 *   auto* state = VALIDATE_STATE(s_registry, nodeId);
 *   if (!state) return;
 */
#define VALIDATE_STATE(registry, nodeId) (registry).get(nodeId)

} // namespace DSP
