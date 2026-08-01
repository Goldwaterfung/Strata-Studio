// src/Core audio engine/sidechain/sidechain_manager_impl.cpp

#include "sidechain_manager_impl.h"
#include <algorithm>
#include <cstring>

namespace Layer3 {

//==============================================================================
// SIDECHAIN BUFFER IMPLEMENTATION
//==============================================================================

SidechainBuffer::SidechainBuffer(NodeID nodeId, uint32_t inputIndex, uint32_t numChannels, uint32_t maxFrames)
    : nodeId_(nodeId)
    , inputIndex_(inputIndex)
    , numChannels_(std::min(numChannels, MAX_CHANNELS))
    , maxFrames_(maxFrames)
{
    channelData_.reserve(numChannels_);
    for (uint32_t c = 0; c < numChannels_; ++c) {
        auto buf = std::make_unique<float[]>(maxFrames_);
        std::memset(buf.get(), 0, maxFrames_ * sizeof(float));
        channels_[c] = buf.get();
        channelData_.push_back(std::move(buf));
    }
}

SidechainBuffer::~SidechainBuffer() = default;

SidechainBuffer::SidechainBuffer(SidechainBuffer&& other) noexcept
    : nodeId_(other.nodeId_)
    , inputIndex_(other.inputIndex_)
    , numChannels_(other.numChannels_)
    , maxFrames_(other.maxFrames_)
    , channelData_(std::move(other.channelData_))
{
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) {
        channels_[c] = (c < numChannels_) ? channelData_[c].get() : nullptr;
    }
    other.nodeId_ = NodeID::invalid();
    other.inputIndex_ = 0;
    other.numChannels_ = 0;
    other.maxFrames_ = 0;
}

SidechainBuffer& SidechainBuffer::operator=(SidechainBuffer&& other) noexcept {
    if (this != &other) {
        nodeId_ = other.nodeId_;
        inputIndex_ = other.inputIndex_;
        numChannels_ = other.numChannels_;
        maxFrames_ = other.maxFrames_;
        channelData_ = std::move(other.channelData_);
        for (uint32_t c = 0; c < MAX_CHANNELS; ++c) {
            channels_[c] = (c < numChannels_) ? channelData_[c].get() : nullptr;
        }
        other.nodeId_ = NodeID::invalid();
        other.inputIndex_ = 0;
        other.numChannels_ = 0;
        other.maxFrames_ = 0;
    }
    return *this;
}

void SidechainBuffer::clear(uint32_t numFrames) {
    uint32_t framesToClear = std::min(numFrames, maxFrames_);
    for (uint32_t c = 0; c < numChannels_; ++c) {
        if (channels_[c]) {
            std::memset(channels_[c], 0, framesToClear * sizeof(float));
        }
    }
}

void SidechainBuffer::accumulate(float* const* inputs, uint32_t numChannels, uint32_t numFrames, float gain) {
    if (!inputs || numChannels == 0) return;
    uint32_t framesToProcess = std::min(numFrames, maxFrames_);
    uint32_t channelsToCopy = std::min(numChannels, numChannels_);

    for (uint32_t c = 0; c < channelsToCopy; ++c) {
        if (!inputs[c] || !channels_[c]) continue;
        const float* __restrict src = inputs[c];
        float* __restrict dst = channels_[c];
        if (gain == 1.0f) {
            for (uint32_t s = 0; s < framesToProcess; ++s) {
                dst[s] += src[s];
            }
        } else {
            for (uint32_t s = 0; s < framesToProcess; ++s) {
                dst[s] += src[s] * gain;
            }
        }
    }
    // Mirror mono channel 0 to aux input channel 1 for stereo destination sidechain
    if (numChannels == 1 && numChannels_ > 1 && inputs[0] && channels_[1]) {
        const float* __restrict src = inputs[0];
        float* __restrict dst = channels_[1];
        if (gain == 1.0f) {
            for (uint32_t s = 0; s < framesToProcess; ++s) {
                dst[s] += src[s];
            }
        } else {
            for (uint32_t s = 0; s < framesToProcess; ++s) {
                dst[s] += src[s] * gain;
            }
        }
    }
}

float* SidechainBuffer::getData(uint32_t channel) {
    return (channel < numChannels_) ? channels_[channel] : nullptr;
}

const float* SidechainBuffer::getData(uint32_t channel) const {
    return (channel < numChannels_) ? channels_[channel] : nullptr;
}

PlanarSidechainBuffer SidechainBuffer::getPlanarBuffer(uint32_t numFrames) const {
    PlanarSidechainBuffer buf{};
    buf.numChannels = numChannels_;
    buf.numFrames = std::min(numFrames, maxFrames_);
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) {
        buf.channels[c] = (c < numChannels_) ? channels_[c] : nullptr;
    }
    return buf;
}

//==============================================================================
// SIDECHAIN MANAGER IMPLEMENTATION
//==============================================================================

std::unique_ptr<ISidechainManager> ISidechainManager::create() {
    return std::make_unique<SidechainManagerImpl>();
}

std::unique_ptr<ISidechainManager> SidechainManagerImpl::create() {
    return std::make_unique<SidechainManagerImpl>();
}

SidechainManagerImpl::SidechainManagerImpl(uint32_t maxFrames)
    : maxFrames_(maxFrames)
    , activeTable_(std::make_shared<SidechainRoutingTable>())
{
    atomicTable_.store(activeTable_.get(), std::memory_order_release);
}

SidechainManagerImpl::~SidechainManagerImpl() = default;

void SidechainManagerImpl::registerSidechainInput(NodeID nodeId, uint32_t inputIndex) {
    std::lock_guard<std::mutex> lock(tableMutex_);

    uint64_t key = SidechainRoutingTable::makeKey(nodeId, inputIndex);

    const SidechainRoutingTable* current = atomicTable_.load(std::memory_order_acquire);
    if (current) {
        for (uint32_t i = 0; i < current->count; ++i) {
            if (current->entries[i].key == key) return;
        }
    }

    auto newTable = std::make_shared<SidechainRoutingTable>();
    if (current) {
        for (uint32_t i = 0; i < current->count && newTable->count < SidechainRoutingTable::MAX_SIDECHAINS; ++i) {
            newTable->entries[newTable->count++] = current->entries[i];
        }
    }

    if (newTable->count < SidechainRoutingTable::MAX_SIDECHAINS) {
        auto buffer = std::make_shared<SidechainBuffer>(nodeId, inputIndex, 2, maxFrames_);
        newTable->entries[newTable->count++] = { key, std::move(buffer) };
    }

    if (activeTable_) {
        gcTables_.push_back(activeTable_);
        if (gcTables_.size() > 8) {
            gcTables_.erase(gcTables_.begin());
        }
    }
    activeTable_ = newTable;
    atomicTable_.store(activeTable_.get(), std::memory_order_release);
}

void SidechainManagerImpl::unregisterSidechainInput(NodeID nodeId, uint32_t inputIndex) {
    std::lock_guard<std::mutex> lock(tableMutex_);

    uint64_t key = SidechainRoutingTable::makeKey(nodeId, inputIndex);

    const SidechainRoutingTable* current = atomicTable_.load(std::memory_order_acquire);
    if (!current) return;

    auto newTable = std::make_shared<SidechainRoutingTable>();
    for (uint32_t i = 0; i < current->count; ++i) {
        if (current->entries[i].key != key) {
            if (newTable->count < SidechainRoutingTable::MAX_SIDECHAINS) {
                newTable->entries[newTable->count++] = current->entries[i];
            }
        }
    }

    if (activeTable_) {
        gcTables_.push_back(activeTable_);
        if (gcTables_.size() > 8) {
            gcTables_.erase(gcTables_.begin());
        }
    }
    activeTable_ = newTable;
    atomicTable_.store(activeTable_.get(), std::memory_order_release);
}

float* SidechainManagerImpl::getSidechainBuffer(NodeID nodeId, uint32_t inputIndex) {
    const SidechainRoutingTable* table = atomicTable_.load(std::memory_order_acquire);
    if (!table) return nullptr;

    uint64_t key = SidechainRoutingTable::makeKey(nodeId, inputIndex);
    for (uint32_t i = 0; i < table->count; ++i) {
        if (table->entries[i].key == key) {
            return table->entries[i].buffer->getData(0);
        }
    }
    return nullptr;
}

PlanarSidechainBuffer SidechainManagerImpl::getSidechainPlanarBuffer(NodeID nodeId, uint32_t inputIndex) {
    const SidechainRoutingTable* table = atomicTable_.load(std::memory_order_acquire);
    if (!table) return PlanarSidechainBuffer{};

    uint64_t key = SidechainRoutingTable::makeKey(nodeId, inputIndex);
    for (uint32_t i = 0; i < table->count; ++i) {
        if (table->entries[i].key == key) {
            return table->entries[i].buffer->getPlanarBuffer(maxFrames_);
        }
    }
    return PlanarSidechainBuffer{};
}

void SidechainManagerImpl::clearAllBuffers(uint32_t numFrames) {
    const SidechainRoutingTable* table = atomicTable_.load(std::memory_order_acquire);
    if (!table) return;

    for (uint32_t i = 0; i < table->count; ++i) {
        if (table->entries[i].buffer) {
            table->entries[i].buffer->clear(numFrames);
        }
    }
}

void SidechainManagerImpl::accumulateSidechainInput(NodeID destNodeId, uint32_t inputIndex, float* const* inputs, uint32_t numChannels, uint32_t numFrames, float gain) {
    const SidechainRoutingTable* table = atomicTable_.load(std::memory_order_acquire);
    if (!table) return;

    uint64_t key = SidechainRoutingTable::makeKey(destNodeId, inputIndex);
    for (uint32_t i = 0; i < table->count; ++i) {
        if (table->entries[i].key == key && table->entries[i].buffer) {
            table->entries[i].buffer->accumulate(inputs, numChannels, numFrames, gain);
            return;
        }
    }
}

} // namespace Layer3
