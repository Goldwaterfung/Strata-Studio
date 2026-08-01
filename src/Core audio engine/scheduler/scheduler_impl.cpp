// src/Core audio engine/scheduler/scheduler_impl.cpp
#include "scheduler_impl.h"
#include "Core audio engine/sidechain/isidechain_manager.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/bridges/itelemetry_bridge.h"
#include "Core infrastructure/memory/aligned_allocator.h"
#include "DSP nodes/monitor_switch/monitor_switch_node.h"
#include "DSP nodes/tracks/audio_track_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/audio_input/audio_input_node.h"
#include "common/dsp/factory_interface.h"
#include "topological_sort.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#endif


namespace Layer3 {

using namespace Layer2;

//==============================================================================
// CONSTRUCTION/DESTRUCTION
//==============================================================================

DSPKernelImpl::DSPKernelImpl(uint32_t initialNodeCapacity)
    : mutationBridge(nullptr), telemetryBridge(nullptr), eventQueue(nullptr),
      liveMidiTargetCount(0), processors(nullptr), processorCapacity(2048),
      topologyBuffer0(nullptr), topologyBuffer1(nullptr),
      activeTopology(nullptr), pendingTopology(nullptr), pdcDelayLines(nullptr),
      pdcCapacity(256), pdcCrossfadeSamples(512), workerRunning(true),
      workerHasWork(false), globalTopologyVersion(0) {
  // Allocate processor registry
  processors = static_cast<ProcessorInfo *>(AlignedAllocator::allocate(
      sizeof(ProcessorInfo) * processorCapacity, 64));
  std::memset(processors, 0, sizeof(ProcessorInfo) * processorCapacity);

  auto initTopology = [initialNodeCapacity](DSPGraphTopology *&topology) {
    topology = static_cast<DSPGraphTopology *>(
        AlignedAllocator::allocate(sizeof(DSPGraphTopology), 64));
    new (topology) DSPGraphTopology();

    topology->nodeCapacity = initialNodeCapacity;
    topology->nodes = static_cast<DSPNode *>(
        AlignedAllocator::allocate(sizeof(DSPNode) * initialNodeCapacity, 64));
    topology->nodeCount.store(0, std::memory_order_relaxed);

    topology->connectionCapacity = initialNodeCapacity * 4;
    topology->connections =
        static_cast<DSPConnection *>(AlignedAllocator::allocate(
            sizeof(DSPConnection) * topology->connectionCapacity, 64));
    topology->connectionCount.store(0, std::memory_order_relaxed);

    topology->executionCapacity = initialNodeCapacity;
    topology->executionOrder = static_cast<uint32_t *>(
        AlignedAllocator::allocate(sizeof(uint32_t) * initialNodeCapacity, 64));
    topology->executionCount.store(0, std::memory_order_relaxed);

    topology->eventBufferCapacity = initialNodeCapacity;
    topology->nodeEventBuffers =
        static_cast<NodeEventBuffer *>(AlignedAllocator::allocate(
            sizeof(NodeEventBuffer) * initialNodeCapacity, 64));
    std::memset(topology->nodeEventBuffers, 0,
                sizeof(NodeEventBuffer) * initialNodeCapacity);

    topology->routingCapacity = initialNodeCapacity * 8; // Adjust as needed
    topology->nodeCumulativeLatencies = static_cast<uint32_t *>(
        AlignedAllocator::allocate(sizeof(uint32_t) * initialNodeCapacity, 64));
    std::memset(topology->nodeCumulativeLatencies, 0,
                sizeof(uint32_t) * initialNodeCapacity);
    topology->routingTable = static_cast<NodeID *>(AlignedAllocator::allocate(
        sizeof(NodeID) * topology->routingCapacity, 64));
    topology->routingOffsets = static_cast<uint32_t *>(
        AlignedAllocator::allocate(sizeof(uint32_t) * initialNodeCapacity, 64));
    std::memset(topology->routingOffsets, 0,
                sizeof(uint32_t) * initialNodeCapacity);

    // Initialize PDC mapping array (one entry per connection)
    uint32_t connectionInitCapacity = initialNodeCapacity * 4;
    topology->connectionPDCIndices =
        static_cast<uint32_t *>(AlignedAllocator::allocate(
            sizeof(uint32_t) * connectionInitCapacity, 64));
    std::memset(topology->connectionPDCIndices, 0xFF,
                sizeof(uint32_t) *
                    connectionInitCapacity); // UINT32_MAX = no PDC

    // Allocate incoming connection adjacency list
    topology->incomingConnectionsCapacity = initialNodeCapacity * 4;
    topology->incomingConnectionIndices = static_cast<uint32_t *>(
        AlignedAllocator::allocate(sizeof(uint32_t) * topology->incomingConnectionsCapacity, 64));
    std::memset(topology->incomingConnectionIndices, 0,
                sizeof(uint32_t) * topology->incomingConnectionsCapacity);

    topology->nodeIncomingCounts = static_cast<uint32_t *>(
        AlignedAllocator::allocate(sizeof(uint32_t) * initialNodeCapacity, 64));
    std::memset(topology->nodeIncomingCounts, 0,
                sizeof(uint32_t) * initialNodeCapacity);

    topology->nodeIncomingOffsets = static_cast<uint32_t *>(
        AlignedAllocator::allocate(sizeof(uint32_t) * initialNodeCapacity, 64));
    std::memset(topology->nodeIncomingOffsets, 0,
                sizeof(uint32_t) * initialNodeCapacity);

    // Allocate pre-sorted node index lookup table
    topology->nodeIndexMap = static_cast<DSPGraphTopology::NodeIndexMapEntry *>(
        AlignedAllocator::allocate(sizeof(DSPGraphTopology::NodeIndexMapEntry) * initialNodeCapacity, 64));
    std::memset(topology->nodeIndexMap, 0,
                sizeof(DSPGraphTopology::NodeIndexMapEntry) * initialNodeCapacity);
    topology->nodeIndexMapCount = 0;

    topology->nodeSilence = static_cast<bool *>(
        AlignedAllocator::allocate(sizeof(bool) * initialNodeCapacity, 64));
    topology->nodeEventCounts = static_cast<uint32_t *>(
        AlignedAllocator::allocate(sizeof(uint32_t) * initialNodeCapacity, 64));

    topology->topologyVersion.store(0, std::memory_order_relaxed);
    topology->maxLatencySamples.store(0, std::memory_order_relaxed);
    topology->hasCycles.store(false, std::memory_order_relaxed);
    topology->needsGrowth.store(false, std::memory_order_relaxed);

    topology->maxChannels = 2; // Default to stereo

    size_t bufferSize = initialNodeCapacity * topology->maxChannels * MAX_SAMPLES_PER_BLOCK * sizeof(float);
    topology->continuousBuffer = static_cast<float *>(AlignedAllocator::allocate(bufferSize, 64));
    std::memset(topology->continuousBuffer, 0, bufferSize);

    topology->nodeOutputPtrs = static_cast<float ***>(AlignedAllocator::allocate(initialNodeCapacity * sizeof(float **), 64));
    for (uint32_t n = 0; n < initialNodeCapacity; ++n) {
      topology->nodeOutputPtrs[n] = static_cast<float **>(AlignedAllocator::allocate(MAX_SUPPORTED_CHANNELS * sizeof(float *), 64));
      for (uint32_t ch = 0; ch < topology->maxChannels; ++ch) {
        topology->nodeOutputPtrs[n][ch] = topology->continuousBuffer + (n * topology->maxChannels + ch) * MAX_SAMPLES_PER_BLOCK;
      }
      for (uint32_t ch = topology->maxChannels; ch < MAX_SUPPORTED_CHANNELS; ++ch) {
        topology->nodeOutputPtrs[n][ch] = nullptr;
      }
    }
  };

  initTopology(topologyBuffer0);
  initTopology(topologyBuffer1);

  activeTopology.store(topologyBuffer0, std::memory_order_release);
  pendingTopology = topologyBuffer1;

  // Allocate PDC delay lines
  pdcDelayLines = static_cast<PDCDelayLine *>(
      AlignedAllocator::allocate(sizeof(PDCDelayLine) * pdcCapacity, 64));
  std::memset(pdcDelayLines, 0, sizeof(PDCDelayLine) * pdcCapacity);

  // Allocate timeline snapshot buffers
  snapshotBuffer0_ = new TimelineSnapshot();
  snapshotBuffer1_ = new TimelineSnapshot();
  snapshotBuffer2_ = new TimelineSnapshot();
  snapshotBuffer3_ = new TimelineSnapshot();
  std::memset(snapshotBuffer0_, 0, sizeof(TimelineSnapshot));
  std::memset(snapshotBuffer1_, 0, sizeof(TimelineSnapshot));
  std::memset(snapshotBuffer2_, 0, sizeof(TimelineSnapshot));
  std::memset(snapshotBuffer3_, 0, sizeof(TimelineSnapshot));
  snapshotBuffers_[0] = snapshotBuffer0_;
  snapshotBuffers_[1] = snapshotBuffer1_;
  snapshotBuffers_[2] = snapshotBuffer2_;
  snapshotBuffers_[3] = snapshotBuffer3_;
  activeSnapshotIdx_.store(0, std::memory_order_release);
  previousActiveSnapshotIdx_.store(0, std::memory_order_release);
  latestSnapshotIdx_.store(0, std::memory_order_release);

  // Allocate scratch input buffers
  size_t scratchSize = MAX_SUPPORTED_CHANNELS * MAX_SAMPLES_PER_BLOCK * sizeof(float);
  scratchInputBuffer = static_cast<float*>(AlignedAllocator::allocate(scratchSize, 64));
  for(uint32_t ch = 0; ch < MAX_SUPPORTED_CHANNELS; ++ch) {
      scratchInputPtrs[ch] = scratchInputBuffer + ch * MAX_SAMPLES_PER_BLOCK;
  }

  // Start worker thread
  workerThread = std::thread(&DSPKernelImpl::workerThreadFunc, this);
}

DSPKernelImpl::~DSPKernelImpl() {
  workerRunning.store(false, std::memory_order_release);

  if (workerThread.joinable()) {
    workerThread.join();
  }

  DSPGraphTopology *active = activeTopology.load(std::memory_order_acquire);
  deallocateTopology(active);
  if (pendingTopology && pendingTopology != active) {
    deallocateTopology(pendingTopology);
  }

  if (processors) {
    AlignedAllocator::deallocate(processors);
  }

  if (pdcDelayLines) {
    // Deallocate individual PDC delay line buffers
    for (uint32_t i = 0; i < pdcCapacity; ++i) {
      if (pdcDelayLines[i].buffer != nullptr) {
        AlignedAllocator::deallocate(pdcDelayLines[i].buffer);
      }
    }
    AlignedAllocator::deallocate(pdcDelayLines);
  }

  delete snapshotBuffer0_;
  delete snapshotBuffer1_;
  delete snapshotBuffer2_;
  delete snapshotBuffer3_;

  if (scratchInputBuffer) {
      AlignedAllocator::deallocate(scratchInputBuffer);
  }
}

void DSPKernelImpl::publishTimelineSnapshot(const TimelineSnapshot &snapshot) {
  uint32_t activeIdx = activeSnapshotIdx_.load(std::memory_order_acquire);
  uint32_t previousIdx = previousActiveSnapshotIdx_.load(std::memory_order_acquire);
  uint32_t latestIdx = latestSnapshotIdx_.load(std::memory_order_acquire);

  uint32_t freeIdx = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    if (i != activeIdx && i != previousIdx && i != latestIdx) {
      freeIdx = i;
      break;
    }
  }

  std::memcpy(snapshotBuffers_[freeIdx], &snapshot, sizeof(TimelineSnapshot));
  latestSnapshotIdx_.store(freeIdx, std::memory_order_release);
}

const TimelineSnapshot *DSPKernelImpl::getActiveTimelineSnapshot() const {
  uint32_t latestIdx = latestSnapshotIdx_.load(std::memory_order_acquire);
  uint32_t activeIdx = activeSnapshotIdx_.load(std::memory_order_relaxed);

  if (latestIdx != activeIdx) {
    previousActiveSnapshotIdx_.store(activeIdx, std::memory_order_release);
    activeSnapshotIdx_.store(latestIdx, std::memory_order_release);
  }

  return snapshotBuffers_[latestIdx];
}

//==============================================================================
// BRIDGE ATTACHMENT
//==============================================================================

void DSPKernelImpl::attachMutationBridge(Layer2::IMutationBridge *bridge) {
  mutationBridge = bridge;
}

void DSPKernelImpl::attachTelemetryBridge(Layer2::ITelemetryBridge *bridge) {
  telemetryBridge = bridge;
}

void DSPKernelImpl::attachEventQueue(Layer2::IEventQueue *queue) {
  eventQueue = queue;
}

void DSPKernelImpl::attachSidechainManager(ISidechainManager *manager) {
  sidechainManager = manager;
}

//==============================================================================
// PROCESSOR REGISTRATION
//==============================================================================

void DSPKernelImpl::registerProcessor(uint32_t nodeType,
                                      DSPProcessFunc processFunc) {
  std::lock_guard<std::mutex> lock(processorMutex);

  if (nodeType >= processorCapacity) {
    // Handle expansion if needed, but for now we assume 64 is enough
    return;
  }

  processors[nodeType].func = processFunc;
}

void DSPKernelImpl::unregisterProcessor(uint32_t nodeType) {
  std::lock_guard<std::mutex> lock(processorMutex);

  if (nodeType < processorCapacity) {
    processors[nodeType].func = nullptr;
  }
}

void DSPKernelImpl::registerFactory(uint32_t nodeType,
                                    DSP::IDSPNodeFactory *factory) {
  std::lock_guard<std::mutex> lock(processorMutex);
  factories[nodeType] = factory;
}

//==============================================================================
// PLUGIN DELAY COMPENSATION
//==============================================================================

uint32_t DSPKernelImpl::getNodeLatency(NodeID nodeId) const {
  // 1. Check explicit manual overrides
  {
    std::lock_guard<std::mutex> lock(latencyMutex);
    auto it = nodeLatencies.find(nodeId.toRaw());
    if (it != nodeLatencies.end()) {
      return it->second;
    }
  }

  // 2. Query factory if available (for nodes that report latency via state)
  DSPGraphTopology *topology = activeTopology.load(std::memory_order_acquire);
  if (topology) {
    uint32_t idx = nodeId.index();
    bool found = false;
    DSPNode node{};
    if (idx < topology->nodeCount.load(std::memory_order_relaxed)) {
      node = topology->nodes[idx];
      if (node.id == nodeId) {
        found = true;
      }
    }
    if (!found) {
      uint32_t count = topology->nodeCount.load(std::memory_order_relaxed);
      for (uint32_t i = 0; i < count; ++i) {
        if (topology->nodes[i].id == nodeId) {
          node = topology->nodes[i];
          found = true;
          break;
        }
      }
    }
    if (found) {
      std::lock_guard<std::mutex> lock(processorMutex);
      auto it = factories.find(node.type);
      if (it != factories.end()) {
        return it->second->getLatency(nodeId);
      }
    }
  }

  return 0;
}

void DSPKernelImpl::setNodeLatency(NodeID nodeId, uint32_t latencySamples) {
  std::lock_guard<std::mutex> lock(latencyMutex);
  nodeLatencies[nodeId.toRaw()] = latencySamples;
}

void DSPKernelImpl::setLiveMIDITargets(const NodeID *targets, uint32_t count) {
  liveMidiTargetCount = (count < 128) ? count : 128;
  for (uint32_t i = 0; i < liveMidiTargetCount; ++i) {
    liveMidiTargets[i] = targets[i];
  }
}

uint32_t DSPKernelImpl::getTotalLatency() const {
  DSPGraphTopology *topology = activeTopology.load(std::memory_order_acquire);
  return topology ? topology->maxLatencySamples.load(std::memory_order_relaxed)
                  : 0;
}

void DSPKernelImpl::applyPDC() {
  std::lock_guard<std::mutex> lock(pendingMutex);
  DSPGraphTopology *topology = pendingTopology;
  if (!topology) {
    return;
  }

  // Ensure execution order is valid before calculating PDC
  rebuildExecutionOrder(topology);
  rebuildRoutingTable(topology);

  uint32_t nodeCount = topology->nodeCount.load(std::memory_order_relaxed);
  if (nodeCount == 0) {
    workerHasWork.store(true, std::memory_order_release);
    return;
  }

  // Allocate temporary array for cumulative latencies (one per node)
  uint32_t *cumulativeLatencies = static_cast<uint32_t *>(
      AlignedAllocator::allocate(sizeof(uint32_t) * nodeCount, 64));
  std::memset(cumulativeLatencies, 0, sizeof(uint32_t) * nodeCount);

  uint32_t executionCount =
      topology->executionCount.load(std::memory_order_relaxed);
  uint32_t maxGraphLatency = 0;
  for (uint32_t i = 0; i < executionCount; ++i) {
    uint32_t nodeIdx = topology->executionOrder[i];
    if (nodeIdx >= nodeCount)
      continue;

    const DSPNode &node = topology->nodes[nodeIdx];
    if (!node.isValid())
      continue;

    // Get this node's intrinsic latency
    uint32_t nodeLatency = 0;
    {
      std::lock_guard<std::mutex> factoryLock(processorMutex);
      auto it = factories.find(node.type);
      if (it != factories.end()) {
        nodeLatency = it->second->getLatency(node.id);
      }
    }

    // Overwrite with manual override if present
    {
      std::lock_guard<std::mutex> latencyLock(latencyMutex);
      auto it = nodeLatencies.find(node.id.toRaw());
      if (it != nodeLatencies.end()) {
        nodeLatency = it->second;
      }
    }

    // Find maximum upstream latency from all incoming connections
    uint32_t maxUpstreamLatency = 0;
    for (uint32_t c = 0;
         c < topology->connectionCount.load(std::memory_order_relaxed); ++c) {
      const DSPConnection &conn = topology->connections[c];
      if (conn.isValid() && conn.destNodeIndex == nodeIdx) {
        if (conn.sourceNodeIndex < nodeCount) {
          uint32_t upstreamLatency = cumulativeLatencies[conn.sourceNodeIndex];
          if (upstreamLatency > maxUpstreamLatency) {
            maxUpstreamLatency = upstreamLatency;
          }
        }
      }
    }

    // Cumulative latency = max upstream latency + this node's latency
    cumulativeLatencies[nodeIdx] = maxUpstreamLatency + nodeLatency;
    topology->nodeCumulativeLatencies[nodeIdx] = cumulativeLatencies[nodeIdx];

    if (cumulativeLatencies[nodeIdx] > maxGraphLatency) {
      maxGraphLatency = cumulativeLatencies[nodeIdx];
    }
  }

  // Update topology max latency
  topology->maxLatencySamples.store(maxGraphLatency, std::memory_order_release);

  // Identify connections needing PDC and allocate/update delay lines
  for (uint32_t n = 0; n < nodeCount; ++n) {
    // For each node, find the max cumulative latency among its inputs
    uint32_t maxInputCumul = 0;
    for (uint32_t c = 0;
         c < topology->connectionCount.load(std::memory_order_relaxed); ++c) {
      const DSPConnection &conn = topology->connections[c];
      if (conn.isValid() && conn.destNodeIndex == n) {
        maxInputCumul =
            std::max(maxInputCumul, cumulativeLatencies[conn.sourceNodeIndex]);
      }
    }

    // Delay each input connection to match the maxInputCumul
    for (uint32_t c = 0;
         c < topology->connectionCount.load(std::memory_order_relaxed); ++c) {
      const DSPConnection &conn = topology->connections[c];
      if (!conn.isValid() || conn.destNodeIndex != n)
        continue;

      uint32_t sourceIdx = conn.sourceNodeIndex;
      uint32_t requiredDelay = maxInputCumul - cumulativeLatencies[sourceIdx];

      if (requiredDelay > 0) {
        uint32_t pdcIndex = topology->connectionPDCIndices[c];
        if (pdcIndex == UINT32_MAX) {
          // Allocate new PDC delay line
          static std::atomic<uint32_t> nextPdcIndex{0};
          uint32_t newIdx = nextPdcIndex.fetch_add(1) % pdcCapacity;
          topology->connectionPDCIndices[c] = newIdx;
          allocatePDCDelayLine(newIdx, requiredDelay + 4096);
          pdcDelayLines[newIdx].setDelay(requiredDelay + 1,
                                         pdcCrossfadeSamples);
        } else {
          pdcDelayLines[pdcIndex].setDelay(requiredDelay + 1,
                                           pdcCrossfadeSamples);
        }
      } else {
        topology->connectionPDCIndices[c] = UINT32_MAX;
      }
    }
  }

  // Cleanup
  AlignedAllocator::deallocate(cumulativeLatencies);

  // Signal worker that pending topology has been updated and needs swap
  workerHasWork.store(true, std::memory_order_release);
}

void DSPKernelImpl::setPDCCrossfade(uint32_t numSamples) {
  pdcCrossfadeSamples = numSamples;
}

//==============================================================================
// AUDIO PROCESSING (RT-SAFE, WAIT-FREE)
//==============================================================================

static uint32_t lookupTopologyIndex(const DSPGraphTopology *topology, NodeID id) {
  if (!topology || !topology->nodeIndexMap)
    return UINT32_MAX;

  uint32_t count = topology->nodeIndexMapCount;
  const auto* first = topology->nodeIndexMap;
  const auto* last = topology->nodeIndexMap + count;

  auto it = std::lower_bound(first, last, id,
                             [](const DSPGraphTopology::NodeIndexMapEntry &entry, NodeID val) {
                               return entry.id.toRaw() < val.toRaw();
                             });

  if (it != last && it->id == id) {
    return it->nodeIdx;
  }
  return UINT32_MAX;
}

void DSPKernelImpl::process(float *const *inputs, float *const *outputs,
                            uint32_t numChannels, uint32_t numSamples,
                            const ProcessContext *context) {
  (void)inputs;  // Reserved for future use (external audio input)
  (void)outputs; // Reserved for future use (main output mix)

  // 1. Check for new topology from worker thread (wait-free)
  DSPGraphTopology *newTopology = nullptr;
  if (workerToRTQueue.pop(newTopology)) {
    DSPGraphTopology *oldTopology =
        activeTopology.load(std::memory_order_acquire);
    activeTopology.store(newTopology, std::memory_order_release);
    rtToWorkerQueue.push(oldTopology);

    // Notify worker thread that buffer has been returned (now via lock-free queue)
  }

  // 2. Pop events from queue (non-blocking, cycle-synchronized)
  constexpr uint32_t MAX_EVENTS = 256;
  thread_local EventData
      eventBuffer[MAX_EVENTS * 2]; // Room for input + routed output events
  uint32_t totalEventCount = 0;
  bool isPreRoll = false;
  if (context) {
    isPreRoll = (static_cast<int64_t>(context->transport.positionSample) < 0);
  }

  if (eventQueue && !isPreRoll) {
    totalEventCount =
        eventQueue->popMultiple(eventBuffer, MAX_EVENTS, numSamples);
  }

  // 3. Get active topology
  DSPGraphTopology *topology = activeTopology.load(std::memory_order_acquire);
  if (!topology || !topology->isValid()) {
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
      std::memset(outputs[ch], 0, numSamples * sizeof(float));
    }
    return;
  }

  // 4. Retrieve pre-allocated per-node output buffers from active topology (no thread_local thrashing)
  uint32_t nodeCount = topology->nodeCount.load(std::memory_order_relaxed);
  uint32_t MAX_NODES = topology->nodeCapacity;
  float*** nodeOutputPtrs = topology->nodeOutputPtrs;

  // Silence tracking array (pre-allocated in topology)
  bool* nodeSilence = topology->nodeSilence;
  std::fill_n(nodeSilence, nodeCount, true);

  // Pre-allocated event counts for dispatching
  uint32_t* nodeEventCounts = topology->nodeEventCounts;
  std::memset(nodeEventCounts, 0, nodeCount * sizeof(uint32_t));

  // Single-pass dispatch of events to per-node event buffers using binary search
  for (uint32_t e = 0; e < totalEventCount; ++e) {
    const EventData &event = eventBuffer[e];
    if (event.targetNodeId.isValid()) {
      uint32_t targetIdx = lookupTopologyIndex(topology, event.targetNodeId);
      if (targetIdx != UINT32_MAX && targetIdx < MAX_NODES) {
        NodeEventBuffer &nodeBuffer = topology->nodeEventBuffers[targetIdx];
        uint32_t nodeCumulativeLatency = topology->nodeCumulativeLatencies[targetIdx];
        int32_t pdcShift = -static_cast<int32_t>(nodeCumulativeLatency);
        int32_t shiftedOffset = static_cast<int32_t>(event.sampleOffset) + pdcShift;
        if (shiftedOffset >= 0 && shiftedOffset < static_cast<int32_t>(numSamples)) {
          if (nodeEventCounts[targetIdx] < nodeBuffer.capacity) {
            uint32_t pos = nodeEventCounts[targetIdx]++;
            nodeBuffer.events[pos] = event;
            nodeBuffer.events[pos].sampleOffset = static_cast<uint32_t>(shiftedOffset);
          } else {
            nodeBuffer.overflowCount.fetch_add(1, std::memory_order_relaxed);
            if (telemetryBridge) {
              uint32_t payload[4] = {nodeEventCounts[targetIdx], nodeBuffer.capacity, 0, 0};
              telemetryBridge->pushTelemetry(event.targetNodeId,
                                             TelemetryFrame::EVENT_OVERFLOW,
                                             payload, 0 /* CRITICAL */);
            }
          }
        }
      }
    } else {
      // Broadcast event: replicate to all live MIDI targets
      for (uint32_t idx = 0; idx < liveMidiTargetCount; ++idx) {
        NodeID targetId = liveMidiTargets[idx];
        uint32_t targetIdx = lookupTopologyIndex(topology, targetId);
        if (targetIdx != UINT32_MAX && targetIdx < MAX_NODES) {
          NodeEventBuffer &nodeBuffer = topology->nodeEventBuffers[targetIdx];
          uint32_t nodeCumulativeLatency = topology->nodeCumulativeLatencies[targetIdx];
          int32_t pdcShift = -static_cast<int32_t>(nodeCumulativeLatency);
          int32_t shiftedOffset = static_cast<int32_t>(event.sampleOffset) + pdcShift;
          if (shiftedOffset >= 0 && shiftedOffset < static_cast<int32_t>(numSamples)) {
            if (nodeEventCounts[targetIdx] < nodeBuffer.capacity) {
              uint32_t pos = nodeEventCounts[targetIdx]++;
              nodeBuffer.events[pos] = event;
              nodeBuffer.events[pos].flags |= 0x80; // Flag as LIVE MIDI
              nodeBuffer.events[pos].targetNodeId = targetId;
              nodeBuffer.events[pos].sampleOffset = static_cast<uint32_t>(shiftedOffset);
            } else {
              nodeBuffer.overflowCount.fetch_add(1, std::memory_order_relaxed);
              if (telemetryBridge) {
                uint32_t payload[4] = {nodeEventCounts[targetIdx], nodeBuffer.capacity, 0, 0};
                telemetryBridge->pushTelemetry(targetId,
                                               TelemetryFrame::EVENT_OVERFLOW,
                                               payload, 0 /* CRITICAL */);
              }
            }
          }
        }
      }
    }
  }

  // 5. Clear active sidechain buffers prior to processing cycle
  if (sidechainManager) {
    sidechainManager->clearAllBuffers(numSamples);
  }

  // 6. Execute nodes in topological order with connection routing
  uint32_t executionCount =
      topology->executionCount.load(std::memory_order_relaxed);
  for (uint32_t i = 0; i < executionCount; ++i) {
    uint32_t nodeIdx = topology->executionOrder[i];
    DSPNode &node = topology->nodes[nodeIdx];

    if (nodeIdx >= MAX_NODES)
      continue; // Safety check

    DSPProcessFunc processFunc = nullptr;
    if (node.type < processorCapacity) {
      processFunc = processors[node.type].func;
    }

    if (!processFunc)
      continue;

    // Evaluate input silence based on incoming connections
    bool inputSilence[MAX_SUPPORTED_CHANNELS];
    std::fill_n(inputSilence, MAX_SUPPORTED_CHANNELS, true);

    uint32_t incomingCount = topology->nodeIncomingCounts[nodeIdx];
    uint32_t incomingOffset = topology->nodeIncomingOffsets[nodeIdx];
    for (uint32_t c = 0; c < incomingCount; ++c) {
      uint32_t connIdx = topology->incomingConnectionIndices[incomingOffset + c];
      const DSPConnection &conn = topology->connections[connIdx];
      uint32_t sourceIdx = conn.sourceNodeIndex;
      if (sourceIdx < nodeCount) {
        if (!nodeSilence[sourceIdx]) {
          if (conn.destPort < topology->maxChannels) {
            inputSilence[conn.destPort] = false;
          }
        }
      }
    }

    // 6. Accumulate input from incoming connections (with PDC if needed)
    for (uint32_t ch = 0; ch < topology->maxChannels; ++ch) {
        std::memset(scratchInputPtrs[ch], 0, numSamples * sizeof(float));
    }


    for (uint32_t c = 0; c < incomingCount; ++c) {
      uint32_t connIdx = topology->incomingConnectionIndices[incomingOffset + c];
      const DSPConnection &conn = topology->connections[connIdx];
      uint32_t sourceIdx = conn.sourceNodeIndex;
      if (sourceIdx >= nodeCount || sourceIdx >= MAX_NODES)
        continue;

      // Check if this connection needs PDC
      uint32_t pdcIndex = topology->connectionPDCIndices[connIdx];
      float gain = conn.gain;

      if (pdcIndex != UINT32_MAX && pdcIndex < pdcCapacity) {
        // Apply PDC delay using block-based processing
        PDCDelayLine &delayLine = pdcDelayLines[pdcIndex];
        uint32_t startDelay = delayLine.currentDelay;
        float startCrossfadeGain = delayLine.crossfadeGain;

        uint32_t srcCh = conn.sourcePort;
        uint32_t dstCh = conn.destPort;
        if (srcCh < topology->maxChannels && dstCh < topology->maxChannels) {
          const float* input = nodeSilence[sourceIdx] ? nullptr : nodeOutputPtrs[sourceIdx][srcCh];
          float* output = scratchInputPtrs[dstCh];
          delayLine.processBlock(input, output, srcCh, numSamples, gain, startDelay, startCrossfadeGain);
        }
        delayLine.advanceGlobalState(numSamples);
      } else if (!nodeSilence[sourceIdx]) {
        // Direct connection (no PDC) - only sum if source is active (SIMD optimized)
        uint32_t srcCh = conn.sourcePort;
        uint32_t dstCh = conn.destPort;
        if (srcCh < topology->maxChannels && dstCh < topology->maxChannels) {
          const float* srcPtr = nodeOutputPtrs[sourceIdx][srcCh];
          float* dstPtr = scratchInputPtrs[dstCh];
          
          #if defined(__APPLE__)
          if (gain == 1.0f) {
              vDSP_vadd(srcPtr, 1, dstPtr, 1, dstPtr, 1, numSamples);
          } else {
              vDSP_vsma(srcPtr, 1, &gain, dstPtr, 1, dstPtr, 1, numSamples);
          }
          #else
          const float* __restrict src = srcPtr;
          float* __restrict dst = dstPtr;
          if (gain == 1.0f) {
              for (uint32_t s = 0; s < numSamples; ++s) dst[s] += src[s];
          } else {
              for (uint32_t s = 0; s < numSamples; ++s) dst[s] += src[s] * gain;
          }
          #endif
        }
      }
    }

    // Get events for this node
    NodeEventBuffer &nodeBuffer = topology->nodeEventBuffers[nodeIdx];
    uint32_t nodeEventCount = nodeEventCounts[nodeIdx];

    // 8. Prepare output event buffer
    constexpr uint32_t MAX_OUT_EVENTS = 64;
    EventData outEvents[MAX_OUT_EVENTS];
    uint32_t outEventCount = 0;

    // 9. Call processing function (reads from scratchInputPtrs,
    // writes back to nodeOutputPtrs[nodeIdx])
    bool isOutputSilent = false;
    ProcessContext currentContext{};
    if (context) {
      currentContext = *context;
    }
    currentContext.sidechainManager = sidechainManager;

    processFunc(node.id, const_cast<float *const *>(scratchInputPtrs),
                const_cast<float *const *>(nodeOutputPtrs[nodeIdx]),
                topology->maxChannels, numSamples, nodeBuffer.events, nodeEventCount,
                outEvents, &outEventCount, &currentContext, inputSilence, &isOutputSilent);
    nodeSilence[nodeIdx] = isOutputSilent;

    // 10. Route output events
    if (outEventCount > 0) {
      routeOutputEvents(nodeIdx, outEvents, outEventCount, nodeEventCounts,
                        MAX_NODES, numSamples);
    }

    // 11. Telemetry (Only push telemetry if node is active)
    if (telemetryBridge && !isOutputSilent) {
      pushNodeTelemetry(node.id,
                        const_cast<float *const *>(nodeOutputPtrs[nodeIdx]),
                        topology->maxChannels, numSamples);
    }
  }

  // 12. Copy final output to output buffers
  uint32_t outputNodeIdx = UINT32_MAX;

  if (context && context->isolateNodeId.isValid()) {
    // Stem isolation: find the specific node
    uint32_t targetIdx = context->isolateNodeId.index();
    if (targetIdx < nodeCount &&
        topology->nodes[targetIdx].id == context->isolateNodeId) {
      outputNodeIdx = targetIdx;
    } else {
      // Collision fallback scan
      for (uint32_t i = 0; i < nodeCount; ++i) {
        if (topology->nodes[i].id == context->isolateNodeId) {
          outputNodeIdx = i;
          break;
        }
      }
    }
  }

  if (outputNodeIdx == UINT32_MAX) {
    // Master mix: copy the last processed node's output
    outputNodeIdx =
        topology->executionCount.load(std::memory_order_relaxed) > 0
            ? topology->executionOrder[topology->executionCount.load(
                                           std::memory_order_relaxed) -
                                       1]
            : 0;
  }

  if (outputNodeIdx < MAX_NODES && outputs != nullptr) {
    for (uint32_t ch = 0; ch < numChannels && ch < topology->maxChannels; ++ch) {
      if (outputs[ch] && nodeOutputPtrs[outputNodeIdx][ch]) {
        std::memcpy(outputs[ch], nodeOutputPtrs[outputNodeIdx][ch],
                    numSamples * sizeof(float));
      }
    }
  }
}

//==============================================================================
// QUERY OPERATIONS
//==============================================================================

uint32_t DSPKernelImpl::getTopologyVersion() const {
  DSPGraphTopology *topology = activeTopology.load(std::memory_order_acquire);
  return topology ? topology->topologyVersion.load(std::memory_order_relaxed)
                  : 0;
}

uint32_t DSPKernelImpl::getNodeCount() const {
  DSPGraphTopology *topology = activeTopology.load(std::memory_order_acquire);
  return topology ? topology->nodeCount.load(std::memory_order_relaxed) : 0;
}

uint32_t DSPKernelImpl::getConnectionCount() const {
  DSPGraphTopology *topology = activeTopology.load(std::memory_order_acquire);
  return topology ? topology->connectionCount.load(std::memory_order_relaxed)
                  : 0;
}

bool DSPKernelImpl::hasCycles() const {
  DSPGraphTopology *topology = activeTopology.load(std::memory_order_acquire);
  return topology ? topology->hasCycles.load(std::memory_order_relaxed) : false;
}

//==============================================================================
// INTERNAL METHODS
//==============================================================================

static uint32_t resolveTopologyIndex(const DSPGraphTopology *topology,
                                     uint32_t packedIndex) {
  uint32_t upper = packedIndex >> 16;
  if (upper >= 1000 && upper <= 2000) {
    // Packed representation: upper 16 bits = NodeType, lower 16 bits =
    // NodeID.id
    uint32_t nodeType = upper;
    uint32_t nodeIdVal = packedIndex & 0xFFFF;

    uint32_t nodeCount = topology->nodeCount.load();
    for (uint32_t i = 0; i < nodeCount; ++i) {
      if (topology->nodes[i].type == nodeType &&
          topology->nodes[i].id.id == nodeIdVal) {
        return i;
      }
    }
  }
  return packedIndex; // Return as-is if not packed or not found
}

void DSPKernelImpl::applyMutation(const SystemMutation &mutation) {
  std::lock_guard<std::mutex> lock(pendingMutex);

  if (wouldExceedCapacity(mutation, pendingTopology)) {
    pendingTopology->needsGrowth.store(true, std::memory_order_release);
    workerHasWork.store(true, std::memory_order_release);
    return;
  }

  uint32_t count = 0;
  switch (mutation.type) {
  case Layer2::MutationType::NODE_ADD:
    count = pendingTopology->nodeCount.load();
    pendingTopology->nodes[count] = mutation.node;
    pendingTopology->nodeEventBuffers[count].capacity =
        estimateMaxEvents(mutation.node.type);
    pendingTopology->nodeEventBuffers[count].events =
        static_cast<EventData *>(AlignedAllocator::allocate(
            sizeof(EventData) *
                pendingTopology->nodeEventBuffers[count].capacity,
            64));
    pendingTopology->nodeCount.store(count + 1, std::memory_order_release);
    break;

  case Layer2::MutationType::NODE_REMOVE: {
    uint32_t targetIdx = UINT32_MAX;
    uint32_t nodeCountVal = pendingTopology->nodeCount.load();
    for (uint32_t i = 0; i < nodeCountVal; ++i) {
      if (pendingTopology->nodes[i].type == mutation.node.type &&
          pendingTopology->nodes[i].id == mutation.node.id) {
        targetIdx = i;
        break;
      }
    }
    if (targetIdx != UINT32_MAX) {

      pendingTopology->nodes[targetIdx].id =
          NodeID::invalid(); // Generation 0 = invalid

      if (pendingTopology->nodeEventBuffers[targetIdx].events) {
        AlignedAllocator::deallocate(
            pendingTopology->nodeEventBuffers[targetIdx].events);
        pendingTopology->nodeEventBuffers[targetIdx].events = nullptr;
      }

      // Invalidate connections involving this node
      for (uint32_t i = 0; i < pendingTopology->connectionCount.load(); ++i) {
        if (pendingTopology->connections[i].sourceNodeIndex == targetIdx ||
            pendingTopology->connections[i].destNodeIndex == targetIdx) {
          pendingTopology->connections[i].gain = -1.0f; // Invalid
        }
      }
    }
    break;
  }

  case Layer2::MutationType::NODE_CONNECT: {
    count = pendingTopology->connectionCount.load();

    DSPConnection conn = mutation.connection;
    uint32_t resolvedSource =
        resolveTopologyIndex(pendingTopology, conn.sourceNodeIndex);
    uint32_t resolvedDest =
        resolveTopologyIndex(pendingTopology, conn.destNodeIndex);

    conn.sourceNodeIndex = resolvedSource;
    conn.destNodeIndex = resolvedDest;

    pendingTopology->connections[count] = conn;

    pendingTopology->connectionCount.store(count + 1,
                                           std::memory_order_release);
    break;
  }

  case Layer2::MutationType::NODE_DISCONNECT: {
    DSPConnection conn = mutation.connection;
    uint32_t resolvedSource =
        resolveTopologyIndex(pendingTopology, conn.sourceNodeIndex);
    uint32_t resolvedDest =
        resolveTopologyIndex(pendingTopology, conn.destNodeIndex);

    conn.sourceNodeIndex = resolvedSource;
    conn.destNodeIndex = resolvedDest;

    for (uint32_t i = 0; i < pendingTopology->connectionCount.load(); ++i) {
      if (pendingTopology->connections[i].sourceNodeIndex ==
              conn.sourceNodeIndex &&
          pendingTopology->connections[i].destNodeIndex == conn.destNodeIndex &&
          pendingTopology->connections[i].sourcePort == conn.sourcePort &&
          pendingTopology->connections[i].destPort == conn.destPort) {
        pendingTopology->connections[i].gain = -1.0f; // Invalid
        break;
      }
    }
    break;
  }

  case Layer2::MutationType::MONITOR_STATE_SET: {
    const bool monitoringActive = (mutation.monitor.monitorState != 0);

    auto updateUpstreamAudioInputs = [&](NodeID targetId, bool active) {
      const uint32_t nodeCountVal = pendingTopology->nodeCount.load();
      const uint32_t connCount = pendingTopology->connectionCount.load();
      for (uint32_t c = 0; c < connCount; ++c) {
        const auto& conn = pendingTopology->connections[c];
        if (!conn.isValid() || conn.destNodeIndex >= nodeCountVal) continue;

        if (pendingTopology->nodes[conn.destNodeIndex].id != targetId) continue;

        const uint32_t srcIdx = conn.sourceNodeIndex;
        if (srcIdx >= nodeCountVal) continue;

        const auto& srcNode = pendingTopology->nodes[srcIdx];
        if (srcNode.type == DSP::NODE_TYPE_AUDIO_INPUT) {
          if (auto* inputState = DSP::AudioInputFactory::getRegistry().get(srcNode.id)) {
            inputState->buffers[0].isMonitoringActive = active;
            inputState->buffers[1].isMonitoringActive = active;
          }
        }
      }
    };

    if (auto* audioTrack = DSP::AudioTrackFactory::getRegistry().get(mutation.targetId)) {
      audioTrack->monitorState = mutation.monitor.monitorState;
      updateUpstreamAudioInputs(mutation.targetId, monitoringActive);
    } else if (auto* monitorSwitch = DSP::MonitorSwitchFactory::getRegistry().get(mutation.targetId)) {
      monitorSwitch->monitorState = mutation.monitor.monitorState;
      updateUpstreamAudioInputs(mutation.targetId, monitoringActive);
    } else if (auto* inst = DSP::getInstrumentSlotState(mutation.targetId)) {
      inst->acceptLiveMIDI = monitoringActive;
    }
    break;
  }

  case Layer2::MutationType::RECORD_ARM_SET: {
    if (auto *s = DSP::AudioInputFactory::getRegistry().get(mutation.targetId)) {
      s->buffers[0].isRecordArmed = mutation.record.isArmed;
      s->buffers[1].isRecordArmed = mutation.record.isArmed;
      s->buffers[0].recordingQueue = static_cast<Layer2::SPSCQueue<float, 524288>*>(mutation.record.recordingQueue);
      s->buffers[1].recordingQueue = static_cast<Layer2::SPSCQueue<float, 524288>*>(mutation.record.recordingQueue);
    }
    break;
  }

  case Layer2::MutationType::SIDECHAIN_CONNECT: {
    if (sidechainManager) {
      sidechainManager->registerSidechainInput(mutation.sidechain.destNodeId, mutation.sidechain.destInputIndex);
    }
    break;
  }

  case Layer2::MutationType::SIDECHAIN_DISCONNECT: {
    if (sidechainManager) {
      sidechainManager->unregisterSidechainInput(mutation.sidechain.destNodeId, mutation.sidechain.destInputIndex);
    }
    break;
  }


  default:
    break;
  }
}

bool DSPKernelImpl::wouldExceedCapacity(
    const SystemMutation &mutation, const DSPGraphTopology *topology) const {
  switch (mutation.type) {
  case Layer2::MutationType::NODE_ADD:
    return topology->nodeCount.load(std::memory_order_relaxed) >=
           topology->nodeCapacity;
  case Layer2::MutationType::NODE_CONNECT:
    return topology->connectionCount.load(std::memory_order_relaxed) >=
           topology->connectionCapacity;
  default:
    return false;
  }
}

uint32_t DSPKernelImpl::estimateMaxEvents(uint32_t nodeType) const {
  (void)nodeType;
  return 128;
}

void DSPKernelImpl::deallocateTopology(DSPGraphTopology *topology) {
  if (!topology)
    return;

  if (topology->nodes)
    AlignedAllocator::deallocate(topology->nodes);
  if (topology->connections)
    AlignedAllocator::deallocate(topology->connections);
  if (topology->executionOrder)
    AlignedAllocator::deallocate(topology->executionOrder);

  if (topology->nodeEventBuffers) {
    for (uint32_t i = 0; i < topology->nodeCount.load(std::memory_order_relaxed); ++i) {
      if (topology->nodeEventBuffers[i].events) {
        AlignedAllocator::deallocate(topology->nodeEventBuffers[i].events);
      }
    }
    AlignedAllocator::deallocate(topology->nodeEventBuffers);
  }

  if (topology->routingTable)
    AlignedAllocator::deallocate(topology->routingTable);
  if (topology->routingOffsets)
    AlignedAllocator::deallocate(topology->routingOffsets);
  if (topology->connectionPDCIndices)
    AlignedAllocator::deallocate(topology->connectionPDCIndices);

  if (topology->nodeCumulativeLatencies)
    AlignedAllocator::deallocate(topology->nodeCumulativeLatencies);

  if (topology->incomingConnectionIndices)
    AlignedAllocator::deallocate(topology->incomingConnectionIndices);
  if (topology->nodeIncomingCounts)
    AlignedAllocator::deallocate(topology->nodeIncomingCounts);
  if (topology->nodeIncomingOffsets)
    AlignedAllocator::deallocate(topology->nodeIncomingOffsets);
  if (topology->nodeIndexMap)
    AlignedAllocator::deallocate(topology->nodeIndexMap);
  if (topology->nodeSilence)
    AlignedAllocator::deallocate(topology->nodeSilence);
  if (topology->nodeEventCounts)
    AlignedAllocator::deallocate(topology->nodeEventCounts);

  if (topology->nodeOutputPtrs) {
    for (uint32_t i = 0; i < topology->nodeCapacity; ++i) {
      if (topology->nodeOutputPtrs[i]) {
        AlignedAllocator::deallocate(topology->nodeOutputPtrs[i]);
      }
    }
    AlignedAllocator::deallocate(topology->nodeOutputPtrs);
  }
  if (topology->continuousBuffer) {
    AlignedAllocator::deallocate(topology->continuousBuffer);
  }

  AlignedAllocator::deallocate(topology);
}

DSPGraphTopology *
DSPKernelImpl::allocateExpandedTopology(const DSPGraphTopology *source, uint32_t newCapacity, uint32_t newMaxChannels) {
  if (!source)
    return nullptr;

  DSPGraphTopology *newTopology = static_cast<DSPGraphTopology *>(
      AlignedAllocator::allocate(sizeof(DSPGraphTopology), 64));
  new (newTopology) DSPGraphTopology();

  newTopology->maxChannels = newMaxChannels;
  newTopology->nodeCapacity = newCapacity;
  newTopology->nodes = static_cast<DSPNode *>(
      AlignedAllocator::allocate(sizeof(DSPNode) * newCapacity, 64));

  uint32_t sourceNodeCount = source->nodeCount.load(std::memory_order_acquire);
  std::memcpy(newTopology->nodes, source->nodes,
              sizeof(DSPNode) * sourceNodeCount);
  std::memset(newTopology->nodes + sourceNodeCount, 0,
              sizeof(DSPNode) * (newCapacity - sourceNodeCount));
  newTopology->nodeCount.store(sourceNodeCount, std::memory_order_release);

  newTopology->connectionCapacity = newCapacity * 4;
  newTopology->connections =
      static_cast<DSPConnection *>(AlignedAllocator::allocate(
          sizeof(DSPConnection) * newTopology->connectionCapacity, 64));

  uint32_t sourceConnCount =
      source->connectionCount.load(std::memory_order_acquire);
  std::memcpy(newTopology->connections, source->connections,
              sizeof(DSPConnection) * sourceConnCount);
  std::memset(newTopology->connections + sourceConnCount, 0,
              sizeof(DSPConnection) *
                  (newTopology->connectionCapacity - sourceConnCount));
  newTopology->connectionCount.store(sourceConnCount,
                                     std::memory_order_release);

  newTopology->executionCapacity = newCapacity;
  newTopology->executionOrder = static_cast<uint32_t *>(
      AlignedAllocator::allocate(sizeof(uint32_t) * newCapacity, 64));
  std::memcpy(newTopology->executionOrder, source->executionOrder,
              sizeof(uint32_t) *
                  source->executionCount.load(std::memory_order_acquire));
  newTopology->executionCount.store(
      source->executionCount.load(std::memory_order_acquire),
      std::memory_order_release);

  newTopology->eventBufferCapacity = newCapacity;
  newTopology->nodeEventBuffers = static_cast<NodeEventBuffer *>(
      AlignedAllocator::allocate(sizeof(NodeEventBuffer) * newCapacity, 64));
  for (uint32_t i = 0; i < newCapacity; ++i) {
    if (i < sourceNodeCount && source->nodeEventBuffers[i].events) {
      uint32_t capacity = source->nodeEventBuffers[i].capacity;
      newTopology->nodeEventBuffers[i].capacity = capacity;
      newTopology->nodeEventBuffers[i].events = static_cast<EventData *>(
          AlignedAllocator::allocate(sizeof(EventData) * capacity, 64));
      std::memcpy(newTopology->nodeEventBuffers[i].events,
                  source->nodeEventBuffers[i].events,
                  sizeof(EventData) * capacity);
      newTopology->nodeEventBuffers[i].overflowCount.store(
          source->nodeEventBuffers[i].overflowCount.load(
              std::memory_order_acquire),
          std::memory_order_release);
    } else {
      newTopology->nodeEventBuffers[i].capacity = 0;
      newTopology->nodeEventBuffers[i].events = nullptr;
      newTopology->nodeEventBuffers[i].overflowCount.store(
          0, std::memory_order_relaxed);
    }
  }

  newTopology->routingCapacity = newCapacity * 8;
  newTopology->routingTable = static_cast<NodeID *>(AlignedAllocator::allocate(
      sizeof(NodeID) * newTopology->routingCapacity, 64));
  newTopology->routingOffsets = static_cast<uint32_t *>(
      AlignedAllocator::allocate(sizeof(uint32_t) * newCapacity, 64));

  newTopology->nodeCumulativeLatencies = static_cast<uint32_t *>(
      AlignedAllocator::allocate(sizeof(uint32_t) * newCapacity, 64));
  std::memcpy(newTopology->nodeCumulativeLatencies,
              source->nodeCumulativeLatencies,
              sizeof(uint32_t) * sourceNodeCount);
  std::memset(newTopology->nodeCumulativeLatencies + sourceNodeCount, 0,
              sizeof(uint32_t) * (newCapacity - sourceNodeCount));

  uint32_t sourceRoutingCapacity = source->routingCapacity;
  std::memcpy(newTopology->routingTable, source->routingTable,
              sizeof(uint32_t) * sourceRoutingCapacity);
  std::memset(newTopology->routingTable + sourceRoutingCapacity, 0,
              sizeof(uint32_t) *
                  (newTopology->routingCapacity - sourceRoutingCapacity));
  std::memcpy(newTopology->routingOffsets, source->routingOffsets,
              sizeof(uint32_t) * sourceNodeCount);
  std::memset(newTopology->routingOffsets + sourceNodeCount, 0,
              sizeof(uint32_t) * (newCapacity - sourceNodeCount));

  // Copy PDC mapping array
  uint32_t newConnectionCapacity = newTopology->connectionCapacity;
  newTopology->connectionPDCIndices = static_cast<uint32_t *>(
      AlignedAllocator::allocate(sizeof(uint32_t) * newConnectionCapacity, 64));
  uint32_t sourceConnectionCount =
      source->connectionCount.load(std::memory_order_acquire);
  std::memcpy(newTopology->connectionPDCIndices, source->connectionPDCIndices,
              sizeof(uint32_t) * sourceConnectionCount);
  std::memset(newTopology->connectionPDCIndices + sourceConnectionCount, 0xFF,
              sizeof(uint32_t) *
                  (newConnectionCapacity - sourceConnectionCount));

  // Allocate and copy incoming connection adjacency list
  newTopology->incomingConnectionsCapacity = newCapacity * 4;
  newTopology->incomingConnectionIndices = static_cast<uint32_t *>(
      AlignedAllocator::allocate(sizeof(uint32_t) * newTopology->incomingConnectionsCapacity, 64));
  std::memcpy(newTopology->incomingConnectionIndices, source->incomingConnectionIndices,
              sizeof(uint32_t) * sourceConnCount);
  std::memset(newTopology->incomingConnectionIndices + sourceConnCount, 0,
              sizeof(uint32_t) * (newTopology->incomingConnectionsCapacity - sourceConnCount));

  newTopology->nodeIncomingCounts = static_cast<uint32_t *>(
      AlignedAllocator::allocate(sizeof(uint32_t) * newCapacity, 64));
  std::memcpy(newTopology->nodeIncomingCounts, source->nodeIncomingCounts,
              sizeof(uint32_t) * sourceNodeCount);
  std::memset(newTopology->nodeIncomingCounts + sourceNodeCount, 0,
              sizeof(uint32_t) * (newCapacity - sourceNodeCount));

  newTopology->nodeIncomingOffsets = static_cast<uint32_t *>(
      AlignedAllocator::allocate(sizeof(uint32_t) * newCapacity, 64));
  std::memcpy(newTopology->nodeIncomingOffsets, source->nodeIncomingOffsets,
              sizeof(uint32_t) * sourceNodeCount);

  newTopology->nodeSilence = static_cast<bool *>(
      AlignedAllocator::allocate(sizeof(bool) * newCapacity, 64));
  newTopology->nodeEventCounts = static_cast<uint32_t *>(
      AlignedAllocator::allocate(sizeof(uint32_t) * newCapacity, 64));
  std::memset(newTopology->nodeIncomingOffsets + sourceNodeCount, 0,
              sizeof(uint32_t) * (newCapacity - sourceNodeCount));

  // Allocate and copy pre-sorted node index lookup table
  newTopology->nodeIndexMap = static_cast<DSPGraphTopology::NodeIndexMapEntry *>(
      AlignedAllocator::allocate(sizeof(DSPGraphTopology::NodeIndexMapEntry) * newCapacity, 64));
  std::memcpy(newTopology->nodeIndexMap, source->nodeIndexMap,
              sizeof(DSPGraphTopology::NodeIndexMapEntry) * source->nodeIndexMapCount);
  std::memset(newTopology->nodeIndexMap + source->nodeIndexMapCount, 0,
              sizeof(DSPGraphTopology::NodeIndexMapEntry) * (newCapacity - source->nodeIndexMapCount));
  newTopology->nodeIndexMapCount = source->nodeIndexMapCount;

  newTopology->topologyVersion.store(
      source->topologyVersion.load(std::memory_order_acquire),
      std::memory_order_release);
  newTopology->maxLatencySamples.store(
      source->maxLatencySamples.load(std::memory_order_acquire),
      std::memory_order_release);
  newTopology->hasCycles.store(
      source->hasCycles.load(std::memory_order_acquire),
      std::memory_order_release);
  newTopology->needsGrowth.store(false, std::memory_order_release);

  size_t bufferSize = newCapacity * newTopology->maxChannels * MAX_SAMPLES_PER_BLOCK * sizeof(float);
  newTopology->continuousBuffer = static_cast<float *>(AlignedAllocator::allocate(bufferSize, 64));
  std::memset(newTopology->continuousBuffer, 0, bufferSize);

  newTopology->nodeOutputPtrs = static_cast<float ***>(AlignedAllocator::allocate(newCapacity * sizeof(float **), 64));
  for (uint32_t n = 0; n < newCapacity; ++n) {
    newTopology->nodeOutputPtrs[n] = static_cast<float **>(AlignedAllocator::allocate(MAX_SUPPORTED_CHANNELS * sizeof(float *), 64));
    for (uint32_t ch = 0; ch < newTopology->maxChannels; ++ch) {
      newTopology->nodeOutputPtrs[n][ch] = newTopology->continuousBuffer + (n * newTopology->maxChannels + ch) * MAX_SAMPLES_PER_BLOCK;
    }
    for (uint32_t ch = newTopology->maxChannels; ch < MAX_SUPPORTED_CHANNELS; ++ch) {
      newTopology->nodeOutputPtrs[n][ch] = nullptr;
    }
  }

  return newTopology;
}

void DSPKernelImpl::rebuildRoutingTable(DSPGraphTopology *topology) {
  std::memset(topology->routingOffsets, 0,
              sizeof(uint32_t) * topology->nodeCapacity);
  uint32_t currentOffset = 0;
  uint32_t nodeCount = topology->nodeCount.load();

  for (uint32_t i = 0; i < nodeCount; ++i) {
    if (!topology->nodes[i].isValid())
      continue;

    topology->routingOffsets[i] = currentOffset;

    uint32_t destCount = 0;
    NodeID *destList = &topology->routingTable[currentOffset + 1];

    for (uint32_t j = 0; j < topology->connectionCount.load(); ++j) {
      if (topology->connections[j].isValid() &&
          topology->connections[j].sourceNodeIndex == i) {

        uint32_t destIdx = topology->connections[j].destNodeIndex;
        if (destIdx < nodeCount &&
            topology->nodes[destIdx].isValid()) {
          if (currentOffset + 1 + destCount < topology->routingCapacity) {
            destList[destCount++] = topology->nodes[destIdx].id;
          }
        }
      }
    }

    topology->routingTable[currentOffset] = NodeID{destCount, 0};
    currentOffset += 1 + destCount;

    if (currentOffset >= topology->routingCapacity)
      break;
  }

  // Pre-compute incoming connection adjacency list for RT thread traversal
  uint32_t connectionCount = topology->connectionCount.load();
  uint32_t incomingOffset = 0;

  std::memset(topology->nodeIncomingCounts, 0, sizeof(uint32_t) * topology->nodeCapacity);
  std::memset(topology->nodeIncomingOffsets, 0, sizeof(uint32_t) * topology->nodeCapacity);

  for (uint32_t i = 0; i < nodeCount; ++i) {
    if (!topology->nodes[i].isValid())
      continue;

    topology->nodeIncomingOffsets[i] = incomingOffset;
    uint32_t count = 0;

    for (uint32_t c = 0; c < connectionCount; ++c) {
      const DSPConnection &conn = topology->connections[c];
      if (conn.isValid() && conn.destNodeIndex == i) {
        if (incomingOffset < topology->incomingConnectionsCapacity) {
          topology->incomingConnectionIndices[incomingOffset] = c;
          incomingOffset++;
          count++;
        }
      }
    }
    topology->nodeIncomingCounts[i] = count;
  }

  // Pre-sort NodeID -> topological index map for binary search lookups
  uint32_t activeNodeCount = 0;
  for (uint32_t i = 0; i < nodeCount; ++i) {
    if (topology->nodes[i].isValid()) {
      topology->nodeIndexMap[activeNodeCount].id = topology->nodes[i].id;
      topology->nodeIndexMap[activeNodeCount].nodeIdx = i;
      activeNodeCount++;
    }
  }

  std::sort(topology->nodeIndexMap, topology->nodeIndexMap + activeNodeCount,
            [](const DSPGraphTopology::NodeIndexMapEntry &a,
               const DSPGraphTopology::NodeIndexMapEntry &b) {
              return a.id.toRaw() < b.id.toRaw();
            });

  topology->nodeIndexMapCount = activeNodeCount;
}

void DSPKernelImpl::routeOutputEvents(uint32_t sourceNodeIdx,
                                      const EventData *events,
                                      uint32_t numEvents,
                                      uint32_t *nodeEventCounts,
                                      uint32_t maxNodes,
                                      uint32_t numSamples) {
  DSPGraphTopology *topology = activeTopology.load(std::memory_order_acquire);
  if (!topology || sourceNodeIdx >= topology->nodeCount.load())
    return;

  uint32_t offset = topology->routingOffsets[sourceNodeIdx];
  uint32_t destCount = topology->routingTable[offset].id;

  for (uint32_t e = 0; e < numEvents; ++e) {
    for (uint32_t d = 0; d < destCount; ++d) {
      NodeID destNodeId = topology->routingTable[offset + 1 + d];
      uint32_t destIdx = lookupTopologyIndex(topology, destNodeId);
      if (destIdx != UINT32_MAX && destIdx < maxNodes) {
        NodeEventBuffer &nodeBuffer = topology->nodeEventBuffers[destIdx];

        uint32_t destCumulativeLatency = topology->nodeCumulativeLatencies[destIdx];
        int32_t destPdcShift = -static_cast<int32_t>(destCumulativeLatency);

        int32_t shiftedOffset = static_cast<int32_t>(events[e].sampleOffset) + destPdcShift;
        if (shiftedOffset >= 0 && shiftedOffset < static_cast<int32_t>(numSamples)) {
          if (nodeEventCounts[destIdx] < nodeBuffer.capacity) {
            uint32_t pos = nodeEventCounts[destIdx]++;
            nodeBuffer.events[pos] = events[e];
            nodeBuffer.events[pos].targetNodeId = destNodeId;
            nodeBuffer.events[pos].sampleOffset = static_cast<uint32_t>(shiftedOffset);
          } else {
            nodeBuffer.overflowCount.fetch_add(1, std::memory_order_relaxed);
            if (telemetryBridge) {
              uint32_t payload[4] = {nodeEventCounts[destIdx], nodeBuffer.capacity, 0, 0};
              telemetryBridge->pushTelemetry(destNodeId,
                                             TelemetryFrame::EVENT_OVERFLOW,
                                             payload, 0 /* CRITICAL */);
            }
          }
        }
      }
    }
  }
}

bool DSPKernelImpl::rebuildExecutionOrder(DSPGraphTopology *topology) {
  uint32_t nodeCount = topology->nodeCount.load();
  uint32_t validNodeCount = 0;
  for (uint32_t i = 0; i < nodeCount; ++i) {
    if (topology->nodes[i].isValid())
      validNodeCount++;
  }

  bool success = TopologicalSort::sortGraph(
      topology->nodes, nodeCount, topology->connections,
      topology->connectionCount.load(), topology->executionOrder,
      topology->executionCapacity);

  if (success) {
    topology->executionCount.store(validNodeCount, std::memory_order_release);
  } else {
    topology->executionCount.store(0, std::memory_order_release);
  }
  return success;
}

static uint32_t calculateMaxChannels(const DSPGraphTopology *topology) {
  uint32_t maxCh = 2; // Always support at least stereo
  
  uint32_t connectionCount = topology->connectionCount.load(std::memory_order_relaxed);
  for (uint32_t i = 0; i < connectionCount; ++i) {
    const auto& conn = topology->connections[i];
    if (conn.isValid()) {
      maxCh = std::max(maxCh, conn.sourcePort + 1);
      maxCh = std::max(maxCh, conn.destPort + 1);
    }
  }
  return std::min(maxCh, MAX_SUPPORTED_CHANNELS);
}

void DSPKernelImpl::workerThreadFunc() {
  std::vector<SystemMutation> currentBatch;

  while (workerRunning.load(std::memory_order_acquire)) {
    bool gotNewMutations = false;

    if (mutationBridge) {
      SystemMutation mutation;
      while (mutationBridge->popMutation(mutation)) {
        currentBatch.push_back(mutation);
        gotNewMutations = true;
      }
    }

    if (gotNewMutations || !currentBatch.empty() ||
        workerHasWork.load(std::memory_order_acquire)) {
      size_t appliedCount = 0;
      if (!currentBatch.empty()) {
        for (const auto &m : currentBatch) {
          if (wouldExceedCapacity(m, pendingTopology)) {
            pendingTopology->needsGrowth.store(true, std::memory_order_release);
            break;
          }
          applyMutation(m);
          appliedCount++;
        }
      }

      // Calculate max channels for the updated pendingTopology
      uint32_t neededMaxChannels = calculateMaxChannels(pendingTopology);
      if (neededMaxChannels != pendingTopology->maxChannels) {
        DSPGraphTopology *newTopology =
            allocateExpandedTopology(pendingTopology, pendingTopology->nodeCapacity, neededMaxChannels);
        if (newTopology) {
          deallocateTopology(pendingTopology);
          pendingTopology = newTopology;
        }
      }

      if (pendingTopology->needsGrowth.load(std::memory_order_acquire)) {
        pendingTopology->needsGrowth.store(false, std::memory_order_release);
        DSPGraphTopology *newTopology =
            allocateExpandedTopology(pendingTopology, pendingTopology->nodeCapacity * 2, pendingTopology->maxChannels);
        if (newTopology) {
          deallocateTopology(pendingTopology);
          pendingTopology = newTopology;
        }
      }

      if (rebuildExecutionOrder(pendingTopology)) {
        rebuildRoutingTable(pendingTopology);
        applyPDC(); // Recalculate PDC after topology changes
        uint32_t nextVersion =
            globalTopologyVersion.fetch_add(1, std::memory_order_relaxed) + 1;
        pendingTopology->topologyVersion.store(nextVersion,
                                               std::memory_order_relaxed);

        // Push to RT and wait for return using condition variable
        workerToRTQueue.push(pendingTopology);

        DSPGraphTopology *returnedBuffer = nullptr;
        // Wait using sleep loop to allow periodic checks for workerRunning
        while (workerRunning.load(std::memory_order_acquire)) {
          if (rtToWorkerQueue.pop(returnedBuffer)) {
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (returnedBuffer) {
          if (returnedBuffer->nodeCapacity < pendingTopology->nodeCapacity ||
              returnedBuffer->maxChannels != pendingTopology->maxChannels) {
            DSPGraphTopology *expandedReturned =
                allocateExpandedTopology(returnedBuffer, pendingTopology->nodeCapacity, pendingTopology->maxChannels);
            if (expandedReturned) {
              deallocateTopology(returnedBuffer);
              returnedBuffer = expandedReturned;
            }
          }
          pendingTopology = returnedBuffer;
          // Apply EXACTLY the same mutations to keep buffers in sync
          for (size_t i = 0; i < appliedCount; ++i) {
            applyMutation(currentBatch[i]);
          }
          // Remove applied mutations from batch
          if (appliedCount > 0) {
            currentBatch.erase(currentBatch.begin(),
                               currentBatch.begin() +
                                   static_cast<std::ptrdiff_t>(appliedCount));
          }
          workerHasWork.store(!currentBatch.empty(), std::memory_order_release);
        }
      } else {
        // Handle cycle or sort error - clear applied mutations to avoid retry
        if (appliedCount > 0) {
          currentBatch.erase(currentBatch.begin(),
                             currentBatch.begin() +
                                 static_cast<std::ptrdiff_t>(appliedCount));
        }
        workerHasWork.store(!currentBatch.empty(), std::memory_order_release);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void DSPKernelImpl::pushNodeTelemetry(NodeID nodeId,
                                      const float *const *outputs,
                                      uint32_t numChannels,
                                      uint32_t numSamples) {
  if (!telemetryBridge || numSamples == 0)
    return;

  for (uint32_t ch = 0; ch < numChannels; ++ch) {
    if (!outputs[ch]) continue;

    float peak = 0.0f;
    float rms = 0.0f;

#if defined(__APPLE__)
    vDSP_maxmgv(outputs[ch], 1, &peak, numSamples);
    vDSP_rmsqv(outputs[ch], 1, &rms, numSamples);
#else
    float sumSquares = 0.0f;
    const float* __restrict input = outputs[ch];
    
    for (uint32_t s = 0; s < numSamples; ++s) {
      float val = std::abs(input[s]);
      if (val > peak) {
        peak = val;
      }
    }
    
    for (uint32_t s = 0; s < numSamples; ++s) {
      float val = input[s];
      sumSquares += val * val;
    }
    
    rms = std::sqrt(sumSquares / static_cast<float>(numSamples));
#endif

    // Peak frame
    auto peakFrame =
        Layer2::TelemetryHelpers::makePeakMeter(nodeId, peak, peak > 1.0f);
    peakFrame.payload[2] = ch; // Channel index
    telemetryBridge->pushTelemetry(peakFrame);

    // RMS frame
    Layer2::ITelemetryBridge::BridgeTelemetryFrame rmsFrame = {};
    rmsFrame.sourceId = nodeId;
    rmsFrame.type = TelemetryFrame::RMS_METER;
    rmsFrame.priority = Layer2::TelemetryPriority::NORMAL;
    uint32_t rmsBits;
    std::memcpy(&rmsBits, &rms, sizeof(float));
    rmsFrame.payload[0] = rmsBits;
    rmsFrame.payload[2] = ch; // Channel index
    telemetryBridge->pushTelemetry(rmsFrame);
  }
}

uint32_t DSPKernelImpl::calculateCumulativeLatency(
    uint32_t nodeIndex, const DSPGraphTopology *topology,
    uint32_t *outCumulativeLatencies) const {
  // Check if already calculated (memoization)
  if (outCumulativeLatencies[nodeIndex] != UINT32_MAX) {
    return outCumulativeLatencies[nodeIndex];
  }

  uint32_t nodeCount = topology->nodeCount.load(std::memory_order_relaxed);
  if (nodeIndex >= nodeCount)
    return 0;

  const DSPNode &node = topology->nodes[nodeIndex];
  if (!node.isValid())
    return 0;

  // Get this node's intrinsic latency
  uint32_t nodeLatency = getNodeLatency(node.id);

  // Find maximum downstream latency from all outgoing connections
  uint32_t maxDownstreamLatency = 0;
  for (uint32_t c = 0;
       c < topology->connectionCount.load(std::memory_order_relaxed); ++c) {
    const DSPConnection &conn = topology->connections[c];
    if (conn.isValid() && conn.sourceNodeIndex == nodeIndex) {
      if (conn.destNodeIndex < nodeCount) {
        uint32_t downstreamLatency = calculateCumulativeLatency(
            conn.destNodeIndex, topology, outCumulativeLatencies);
        if (downstreamLatency > maxDownstreamLatency) {
          maxDownstreamLatency = downstreamLatency;
        }
      }
    }
  }

  // Store and return cumulative latency
  uint32_t cumulativeLatency = nodeLatency + maxDownstreamLatency;
  outCumulativeLatencies[nodeIndex] = cumulativeLatency;
  return cumulativeLatency;
}

bool DSPKernelImpl::allocatePDCDelayLine(uint32_t pdcIndex,
                                         uint32_t maxDelaySamples) {
  if (pdcIndex >= pdcCapacity)
    return false;

  // Free existing buffer if present
  if (pdcDelayLines[pdcIndex].buffer != nullptr) {
    AlignedAllocator::deallocate(pdcDelayLines[pdcIndex].buffer);
    pdcDelayLines[pdcIndex].buffer = nullptr;
  }

  // Allocate new buffer with alignment for SIMD (supporting 16 channels)
  pdcDelayLines[pdcIndex].buffer = static_cast<float *>(
      AlignedAllocator::allocate(sizeof(float) * maxDelaySamples * MAX_SUPPORTED_CHANNELS, 64));

  if (pdcDelayLines[pdcIndex].buffer == nullptr) {
    return false;
  }

  // Initialize buffer to silence
  std::memset(pdcDelayLines[pdcIndex].buffer, 0,
              sizeof(float) * maxDelaySamples * MAX_SUPPORTED_CHANNELS);

  // Initialize delay line state
  pdcDelayLines[pdcIndex].capacity = maxDelaySamples;
  std::memset(pdcDelayLines[pdcIndex].writePos, 0, sizeof(uint32_t) * MAX_SUPPORTED_CHANNELS);
  pdcDelayLines[pdcIndex].currentDelay = 0;
  pdcDelayLines[pdcIndex].targetDelay = 0;
  pdcDelayLines[pdcIndex].crossfadeGain = 0.0f;
  pdcDelayLines[pdcIndex].crossfadeSamples = pdcCrossfadeSamples;

  return true;
}

std::unique_ptr<IDSPKernel> IDSPKernel::create(uint32_t initialNodeCapacity) {
  return std::unique_ptr<IDSPKernel>(new DSPKernelImpl(initialNodeCapacity));
}

} // namespace Layer3
