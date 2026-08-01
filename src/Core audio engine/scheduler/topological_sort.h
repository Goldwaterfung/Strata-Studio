// src/Core audio engine/scheduler/topological_sort.h
#pragma once

#include "system_primitives.h"
#include <vector>
#include <queue>
#include <functional>

namespace Layer3 {

//==============================================================================
// TOPOLOGICAL SORT (Kahn's Algorithm)
//==============================================================================

class TopologicalSort {
public:
    // Sort graph using Kahn's algorithm
    // Parameters:
    //   nodes: Node array
    //   nodeCount: Number of nodes
    //   connections: Connection array
    //   connectionCount: Number of connections
    //   outOrder: Output array for sorted node indices
    //   outOrderCapacity: Maximum entries in output array
    // Returns: true if sort succeeded (no cycles), false if cycle detected
    // Thread-safety: Thread-safe (no shared state)
    static bool sortGraph(const DSPNode* nodes,
                         uint32_t nodeCount,
                         const DSPConnection* connections,
                         uint32_t connectionCount,
                         uint32_t* outOrder,
                         uint32_t outOrderCapacity);

    // Detect cycles using DFS
    // Returns: true if graph contains cycle
    // Thread-safety: Thread-safe (no shared state)
    static bool hasCycle(const DSPNode* nodes,
                        uint32_t nodeCount,
                        const DSPConnection* connections,
                        uint32_t connectionCount);
};

} // namespace Layer3
