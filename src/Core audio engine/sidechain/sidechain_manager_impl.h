// src/Core audio engine/sidechain/sidechain_manager_impl.h
#pragma once

#include "isidechain_manager.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Layer3 {

//==============================================================================
// SIDECHAIN BUFFER (Internal State)
//==============================================================================

class SidechainBuffer {
public:
    //==========================================================================
    // Construction
    //==========================================================================

    explicit SidechainBuffer(NodeID nodeId, uint32_t inputIndex, uint32_t numChannels = 2, uint32_t maxFrames = 4096);
    ~SidechainBuffer();

    // Disable copy, enable move
    SidechainBuffer(const SidechainBuffer&) = delete;
    SidechainBuffer& operator=(const SidechainBuffer&) = delete;
    SidechainBuffer(SidechainBuffer&& other) noexcept;
    SidechainBuffer& operator=(SidechainBuffer&& other) noexcept;

    //==========================================================================
    // Operations (RT-Safe)
    //==========================================================================

    void clear(uint32_t numFrames);
    void accumulate(float* const* inputs, uint32_t numChannels, uint32_t numFrames, float gain);

    //==========================================================================
    // Accessors (RT-Safe)
    //==========================================================================

    float* getData(uint32_t channel = 0);
    const float* getData(uint32_t channel = 0) const;
    PlanarSidechainBuffer getPlanarBuffer(uint32_t numFrames) const;
    NodeID getNodeId() const { return nodeId_; }
    uint32_t getInputIndex() const { return inputIndex_; }
    uint32_t getNumChannels() const { return numChannels_; }

private:
    NodeID nodeId_;
    uint32_t inputIndex_;
    uint32_t numChannels_;
    uint32_t maxFrames_;

    // Pre-allocated multi-channel planar audio buffers
    std::vector<std::unique_ptr<float[]>> channelData_;
    float* channels_[MAX_CHANNELS]{nullptr};
};

//==============================================================================
// SIDECHAIN ROUTING TABLE (Lock-Free State)
//==============================================================================

struct SidechainRoutingTable {
    static constexpr uint32_t MAX_SIDECHAINS = 128;
    struct Entry {
        uint64_t key;
        std::shared_ptr<SidechainBuffer> buffer;
    };
    std::array<Entry, MAX_SIDECHAINS> entries;
    uint32_t count = 0;

    static uint64_t makeKey(NodeID nodeId, uint32_t inputIndex) {
        return (static_cast<uint64_t>(nodeId.toPacked()) << 32) | inputIndex;
    }
};

//==============================================================================
// SIDECHAIN MANAGER IMPLEMENTATION
//==============================================================================

class SidechainManagerImpl : public ISidechainManager {
public:
    //==========================================================================
    // Factory
    //==========================================================================

    static std::unique_ptr<ISidechainManager> create();

    //==========================================================================
    // Construction/Destruction
    //==========================================================================

    explicit SidechainManagerImpl(uint32_t maxFrames = 4096);
    ~SidechainManagerImpl() override;

    // Disable copy and move
    SidechainManagerImpl(const SidechainManagerImpl&) = delete;
    SidechainManagerImpl& operator=(const SidechainManagerImpl&) = delete;
    SidechainManagerImpl(SidechainManagerImpl&&) = delete;
    SidechainManagerImpl& operator=(SidechainManagerImpl&&) = delete;

    //==========================================================================
    // ISidechainManager Implementation
    //==========================================================================

    void registerSidechainInput(NodeID nodeId, uint32_t inputIndex) override;
    void unregisterSidechainInput(NodeID nodeId, uint32_t inputIndex) override;
    float* getSidechainBuffer(NodeID nodeId, uint32_t inputIndex) override;
    PlanarSidechainBuffer getSidechainPlanarBuffer(NodeID nodeId, uint32_t inputIndex) override;
    void clearAllBuffers(uint32_t numFrames) override;
    void accumulateSidechainInput(NodeID destNodeId, uint32_t inputIndex, 
                                  float* const* inputs, uint32_t numChannels, 
                                  uint32_t numFrames, float gain) override;

private:
    uint32_t maxFrames_;

    // Mutex for non-RT table mutations
    std::mutex tableMutex_;

    // RCU routing table state
    std::shared_ptr<const SidechainRoutingTable> activeTable_;
    std::atomic<const SidechainRoutingTable*> atomicTable_{nullptr};

    // Garbage collection vector to keep retired tables alive while RT reads
    std::vector<std::shared_ptr<const SidechainRoutingTable>> gcTables_;
};

} // namespace Layer3
