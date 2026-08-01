#include <catch2/catch_test_macros.hpp>
#include "Core audio engine/scheduler/topological_sort.h"
#include <vector>

using namespace Layer3;

TEST_CASE("TopologicalSort: Linear Chain", "[Layer3][Scheduler]") {
    // A -> B -> C
    DSPNode nodes[3];
    nodes[0].id = {0, 1};
    nodes[1].id = {1, 1};
    nodes[2].id = {2, 1};

    DSPConnection conns[2];
    conns[0].sourceNodeIndex = 0; // A
    conns[0].destNodeIndex = 1;   // B
    conns[1].sourceNodeIndex = 1; // B
    conns[1].destNodeIndex = 2;   // C

    uint32_t order[3];
    bool success = TopologicalSort::sortGraph(nodes, 3, conns, 2, order, 3);

    REQUIRE(success == true);
    CHECK(order[0] == 0); // A must be first
    CHECK(order[1] == 1); // B must be second
    CHECK(order[2] == 2); // C must be third
}

TEST_CASE("TopologicalSort: Diamond Graph", "[Layer3][Scheduler]") {
    // A -> B, A -> C, B -> D, C -> D
    DSPNode nodes[4];
    for (uint32_t i = 0; i < 4; ++i) nodes[i].id = {i, 1};

    DSPConnection conns[4];
    conns[0] = {0, 0, 1, 0, 1.0f}; // A -> B
    conns[1] = {0, 0, 2, 0, 1.0f}; // A -> C
    conns[2] = {1, 0, 3, 0, 1.0f}; // B -> D
    conns[3] = {2, 0, 3, 0, 1.0f}; // C -> D

    uint32_t order[4];
    bool success = TopologicalSort::sortGraph(nodes, 4, conns, 4, order, 4);

    REQUIRE(success == true);
    
    // Constraints:
    // 0 before 1, 0 before 2
    // 1 before 3, 2 before 3
    
    auto getPos = [&](uint32_t index) {
        for (int i = 0; i < 4; ++i) if (order[i] == index) return i;
        return -1;
    };

    CHECK(getPos(0) < getPos(1));
    CHECK(getPos(0) < getPos(2));
    CHECK(getPos(1) < getPos(3));
    CHECK(getPos(2) < getPos(3));
}

TEST_CASE("TopologicalSort: Parallel Fan-out", "[Layer3][Scheduler]") {
    // A -> {B, C, D, E, F}
    DSPNode nodes[6];
    for (uint32_t i = 0; i < 6; ++i) nodes[i].id = {i, 1};

    DSPConnection conns[5];
    for (uint32_t i = 0; i < 5; ++i) {
        conns[i] = {0, 0, i + 1, 0, 1.0f}; // A -> B, A -> C, ...
    }

    uint32_t order[6];
    bool success = TopologicalSort::sortGraph(nodes, 6, conns, 5, order, 6);

    REQUIRE(success == true);
    CHECK(order[0] == 0); // A must be first
    
    // B, C, D, E, F can be in any order after A
}

TEST_CASE("TopologicalSort: Cyclic Graph", "[Layer3][Scheduler]") {
    // A -> B -> C -> A
    DSPNode nodes[3];
    for (uint32_t i = 0; i < 3; ++i) nodes[i].id = {i, 1};

    DSPConnection conns[3];
    conns[0] = {0, 0, 1, 0, 1.0f}; // A -> B
    conns[1] = {1, 0, 2, 0, 1.0f}; // B -> C
    conns[2] = {2, 0, 0, 0, 1.0f}; // C -> A

    uint32_t order[3];
    bool success = TopologicalSort::sortGraph(nodes, 3, conns, 3, order, 3);
    CHECK(success == false);

    bool hasCycle = TopologicalSort::hasCycle(nodes, 3, conns, 3);
    CHECK(hasCycle == true);
}

TEST_CASE("TopologicalSort: Disconnected Nodes", "[Layer3][Scheduler]") {
    // A -> B, C
    DSPNode nodes[3];
    for (uint32_t i = 0; i < 3; ++i) nodes[i].id = {i, 1};

    DSPConnection conns[1];
    conns[0] = {0, 0, 1, 0, 1.0f}; // A -> B

    uint32_t order[3];
    bool success = TopologicalSort::sortGraph(nodes, 3, conns, 1, order, 3);

    REQUIRE(success == true);
    
    auto getPos = [&](uint32_t index) {
        for (int i = 0; i < 3; ++i) if (order[i] == index) return i;
        return -1;
    };

    CHECK(getPos(0) < getPos(1)); // A before B
    // C can be anywhere
}
