// src/Core audio engine/scheduler/topological_sort.cpp
#include "topological_sort.h"
#include <algorithm>
#include <functional>

namespace Layer3 {

//==============================================================================
// TOPOLOGICAL SORT IMPLEMENTATION
//==============================================================================

bool TopologicalSort::sortGraph(const DSPNode* nodes,
                               uint32_t nodeCount,
                               const DSPConnection* connections,
                               uint32_t connectionCount,
                               uint32_t* outOrder,
                               uint32_t outOrderCapacity)
{
    (void)nodes;
    if (nodeCount == 0) {
        return true;
    }
    
    if (nodeCount > outOrderCapacity) {
        return false;
    }

    // 1. Calculate in-degrees
    std::vector<int> inDegree(nodeCount, 0);
    for (uint32_t i = 0; i < connectionCount; ++i) {
        if (!connections[i].isValid()) continue;
        
        uint32_t sourceIdx = connections[i].sourceNodeIndex;
        uint32_t destIdx = connections[i].destNodeIndex;
        
        if (sourceIdx < nodeCount && destIdx < nodeCount && 
            nodes[sourceIdx].isValid() && nodes[destIdx].isValid()) {
            inDegree[destIdx]++;
        }
    }

    // 2. Queue nodes with 0 in-degree
    std::queue<uint32_t> q;
    for (uint32_t i = 0; i < nodeCount; ++i) {
        if (nodes[i].isValid()) {
            if (inDegree[i] == 0) {
                q.push(i);
            }
        }
    }

    // 3. Process queue
    uint32_t outputCount = 0;
    while (!q.empty()) {
        uint32_t u = q.front();
        q.pop();
        
        outOrder[outputCount++] = u;
        
        // Find all outgoing connections from u
        for (uint32_t i = 0; i < connectionCount; ++i) {
            if (!connections[i].isValid()) continue;
            
            if (connections[i].sourceNodeIndex == u) {
                uint32_t v = connections[i].destNodeIndex;
                if (v < nodeCount && nodes[v].isValid()) {
                    if (--inDegree[v] == 0) {
                        q.push(v);
                    }
                }
            }
        }
    }

    // 4. Check for cycles (not all nodes processed)
    uint32_t validNodeCount = 0;
    for (uint32_t i = 0; i < nodeCount; ++i) {
        if (nodes[i].isValid()) validNodeCount++;
    }
    return outputCount == validNodeCount;
}

bool TopologicalSort::hasCycle(const DSPNode* nodes,
                               uint32_t nodeCount,
                               const DSPConnection* connections,
                               uint32_t connectionCount)
{
    (void)nodes;
    if (nodeCount == 0) return false;

    // DFS-based cycle detection
    enum NodeState { UNVISITED, VISITING, VISITED };
    std::vector<NodeState> state(nodeCount, UNVISITED);
    
    std::function<bool(uint32_t)> dfs = [&](uint32_t u) -> bool {
        state[u] = VISITING;
        
        // Visit all neighbors
        for (uint32_t i = 0; i < connectionCount; ++i) {
            if (connections[i].sourceNodeIndex == u) {
                uint32_t v = connections[i].destNodeIndex;
                if (v < nodeCount) {
                    if (state[v] == VISITING) {
                        return true;  // Back edge = cycle
                    }
                    if (state[v] == UNVISITED && dfs(v)) {
                        return true;
                    }
                }
            }
        }
        
        state[u] = VISITED;
        return false;
    };

    for (uint32_t i = 0; i < nodeCount; ++i) {
        if (state[i] == UNVISITED && dfs(i)) {
            return true;
        }
    }
    
    return false;
}

} // namespace Layer3
