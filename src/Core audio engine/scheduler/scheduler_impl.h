// src/Core audio engine/scheduler/scheduler_impl.h
#pragma once

#include "Core infrastructure/bridges/mpsc_queue.h"
#include "Core infrastructure/bridges/spsc_queue.h"
#include "idsp_kernel.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstring>

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#endif

namespace Layer3 {

constexpr uint32_t MAX_SUPPORTED_CHANNELS = 16;
constexpr uint32_t MAX_SAMPLES_PER_BLOCK = 1024;

//==============================================================================
// INTERNAL IMPLEMENTATION
//==============================================================================

// Processor registry entry
struct ProcessorInfo {
  DSPProcessFunc func;      // Processing function
  uint32_t stateSize;       // Size of state (for future use)
  uint32_t maxOutputEvents; // Maximum output events per process call
  uint32_t reserved;        // Reserved
};

// Per-node event buffer (pre-allocated to prevent silent drops)
struct NodeEventBuffer {
  EventData *events; // Pre-allocated event array
  uint32_t capacity; // Maximum events (estimated at graph build)
  std::atomic<uint32_t>
      overflowCount; // Counter for dropped events (for telemetry)
};

// PDC Delay line with crossfade compensation (prevents clicks when delay
// changes)
struct PDCDelayLine {
  float *buffer;             // Circular buffer of size capacity * MAX_SUPPORTED_CHANNELS
  uint32_t capacity;         // Total buffer size per channel
  uint32_t writePos[MAX_SUPPORTED_CHANNELS];     // Current write position per channel
  uint32_t currentDelay;     // Current delay in samples
  uint32_t targetDelay;      // Target delay (for crossfade)
  float crossfadeGain;       // Crossfade progress (0.0 to 1.0)
  uint32_t crossfadeSamples; // Total crossfade duration


  // Block-based delay processing with SIMD and phase alignment support
  void processBlock(
      const float* input,
      float* output,
      uint32_t ch,
      uint32_t numSamples,
      float gain,
      uint32_t startDelay,
      float startCrossfadeGain
  ) {
    if (ch >= MAX_SUPPORTED_CHANNELS || !buffer) return;

    // 1. Contiguous Circular Write
    uint32_t w = writePos[ch];
    uint32_t L1 = std::min(numSamples, capacity - w);
    if (input) {
        std::memcpy(&buffer[ch * capacity + w], input, L1 * sizeof(float));
        if (numSamples > L1) {
            std::memcpy(&buffer[ch * capacity], input + L1, (numSamples - L1) * sizeof(float));
        }
    } else {
        std::memset(&buffer[ch * capacity + w], 0, L1 * sizeof(float));
        if (numSamples > L1) {
            std::memset(&buffer[ch * capacity], 0, (numSamples - L1) * sizeof(float));
        }
    }
    writePos[ch] = (w + numSamples) % capacity;

    // 2. Read and Sum
    if (startDelay == targetDelay) {
        // Fast SIMD Path
        uint32_t r = (w + 1 - startDelay + capacity) % capacity;
        uint32_t R1 = std::min(numSamples, capacity - r);
        
        #if defined(__APPLE__)
        if (gain == 1.0f) {
            vDSP_vadd(&buffer[ch * capacity + r], 1, output, 1, output, 1, R1);
            if (numSamples > R1) {
                vDSP_vadd(&buffer[ch * capacity], 1, output + R1, 1, output + R1, 1, numSamples - R1);
            }
        } else {
            vDSP_vsma(&buffer[ch * capacity + r], 1, &gain, output, 1, output, 1, R1);
            if (numSamples > R1) {
                vDSP_vsma(&buffer[ch * capacity], 1, &gain, output + R1, 1, output + R1, 1, numSamples - R1);
            }
        }
        #else
        const float* __restrict src1 = &buffer[ch * capacity + r];
        float* __restrict dst1 = output;
        if (gain == 1.0f) {
            for (uint32_t s = 0; s < R1; ++s) {
                dst1[s] += src1[s];
            }
            if (numSamples > R1) {
                const float* __restrict src2 = &buffer[ch * capacity];
                float* __restrict dst2 = output + R1;
                uint32_t R2 = numSamples - R1;
                for (uint32_t s = 0; s < R2; ++s) {
                    dst2[s] += src2[s];
                }
            }
        } else {
            for (uint32_t s = 0; s < R1; ++s) {
                dst1[s] += src1[s] * gain;
            }
            if (numSamples > R1) {
                const float* __restrict src2 = &buffer[ch * capacity];
                float* __restrict dst2 = output + R1;
                uint32_t R2 = numSamples - R1;
                for (uint32_t s = 0; s < R2; ++s) {
                    dst2[s] += src2[s] * gain;
                }
            }
        }
        #endif
    } else {
        // Phase-aligned, modulo-free crossfade loop
        float localGain = startCrossfadeGain;
        uint32_t localDelay = startDelay;

        uint32_t r1 = (w + 1 - localDelay + capacity) % capacity;
        uint32_t r2 = (w + 1 - targetDelay + capacity) % capacity;

        for (uint32_t s = 0; s < numSamples; ++s) {
            float oldSample = buffer[ch * capacity + r1];
            float newSample = buffer[ch * capacity + r2];
            float sample = oldSample * (1.0f - localGain) + newSample * localGain;
            
            output[s] += sample * gain;

            // Advance pointer indices
            r1 = (r1 + 1 == capacity) ? 0 : r1 + 1;
            r2 = (r2 + 1 == capacity) ? 0 : r2 + 1;

            // Advance local crossfade parameters
            localGain += 1.0f / static_cast<float>(crossfadeSamples);
            if (localGain >= 1.0f) {
                localDelay = targetDelay;
                localGain = 0.0f;
                r1 = r2;
            }
        }
    }
  }

  // Advance the global state once at the end of the block processing cycle
  void advanceGlobalState(uint32_t numSamples) {
      if (currentDelay != targetDelay) {
          crossfadeGain += static_cast<float>(numSamples) / static_cast<float>(crossfadeSamples);
          if (crossfadeGain >= 1.0f) {
              currentDelay = targetDelay;
              crossfadeGain = 0.0f;
          }
      }
  }

  // Set new target delay (triggers crossfade)
  void setDelay(uint32_t newDelay, uint32_t crossfadeDuration) {
    if (newDelay != currentDelay && newDelay != targetDelay) {
      targetDelay = newDelay;
      crossfadeSamples = crossfadeDuration;
      crossfadeGain = 0.0f;
    }
  }
};

// Complete graph topology (double-buffered with growth capability)
struct DSPGraphTopology {
  // Node storage
  DSPNode *nodes;                  // Aligned node array
  uint32_t nodeCapacity;           // Maximum nodes
  std::atomic<uint32_t> nodeCount; // Current node count

  // Connection storage
  DSPConnection *connections;            // Aligned connection array
  uint32_t connectionCapacity;           // Maximum connections
  std::atomic<uint32_t> connectionCount; // Current connection count

  // Execution order
  uint32_t *executionOrder;             // Sorted node indices
  uint32_t executionCapacity;           // Maximum execution order entries
  std::atomic<uint32_t> executionCount; // Current execution order count

  // Per-node event buffers (pre-allocated)
  NodeEventBuffer *nodeEventBuffers; // Array of per-node buffers
  uint32_t eventBufferCapacity;      // Size of nodeEventBuffers array

  // PDC cumulative latencies (one per node)
  uint32_t *nodeCumulativeLatencies; // Latency from node to graph end

  // Output event routing table (for plugin->plugin MIDI/event routing)
  NodeID *routingTable; // Flat routing table: [sourceNode][destCount][destNodes...]
  uint32_t *routingOffsets; // Offsets into routingTable for each source node
  uint32_t routingCapacity; // Total routing table capacity

  // PDC delay line mapping (one entry per connection)
  uint32_t *connectionPDCIndices; // Maps connectionIndex -> PDC delay line
                                  // index (UINT32_MAX = no PDC)

  // Incoming connection adjacency list
  uint32_t *incomingConnectionIndices;  // Flat array of connections mapping to destination node indices
  uint32_t incomingConnectionsCapacity; // Capacity of incomingConnectionIndices
  uint32_t *nodeIncomingCounts;          // Count of incoming connections per node
  uint32_t *nodeIncomingOffsets;         // Offset into incomingConnectionIndices per node

  // Pre-sorted NodeID -> topological index lookup table
  struct NodeIndexMapEntry {
    NodeID id;
    uint32_t nodeIdx;
  };
  NodeIndexMapEntry *nodeIndexMap;      // Pre-sorted array of active node mapping
  uint32_t nodeIndexMapCount;           // Number of entries in nodeIndexMap

  // Pre-allocated output buffers to avoid thread_local thrashing
  float *continuousBuffer; // Flat buffer for all nodes/channels/samples: nodeCapacity * maxChannels * 1024
  float ***nodeOutputPtrs; // Pointers mapping: nodeIndex -> channelPtr -> sampleBuffer
  uint32_t maxChannels;    // Dynamic channel count for the session (default = 2)

  // Temporary arrays for RT processing (pre-allocated)
  bool *nodeSilence;
  uint32_t *nodeEventCounts;

  // Metadata
  std::atomic<uint32_t> topologyVersion;   // Version counter
  std::atomic<uint32_t> maxLatencySamples; // Maximum latency in graph
  std::atomic<bool> hasCycles;             // Cycle detection flag
  std::atomic<bool> needsGrowth;           // Flag indicating capacity exceeded

  // Validation
  bool isValid() const {
    return nodes != nullptr && connections != nullptr &&
           executionOrder != nullptr && nodeEventBuffers != nullptr &&
           incomingConnectionIndices != nullptr && nodeIncomingCounts != nullptr &&
           nodeIncomingOffsets != nullptr && nodeIndexMap != nullptr &&
           continuousBuffer != nullptr && nodeOutputPtrs != nullptr &&
           nodeSilence != nullptr && nodeEventCounts != nullptr;
  }
};

// DSP Kernel Implementation
class DSPKernelImpl : public Layer3::IDSPKernel {
public:
  //==========================================================================
  // Construction/Destruction
  //==========================================================================

  explicit DSPKernelImpl(uint32_t initialNodeCapacity);
  ~DSPKernelImpl() override;

  //==========================================================================
  // IDSPKernel Implementation
  //==========================================================================

  void attachMutationBridge(Layer2::IMutationBridge *bridge) override;
  void attachTelemetryBridge(Layer2::ITelemetryBridge *bridge) override;
  void attachEventQueue(Layer2::IEventQueue *eventQueue) override;
  void attachSidechainManager(ISidechainManager *sidechainManager) override;

  void registerProcessor(uint32_t nodeType,
                         DSPProcessFunc processFunc) override;
  void unregisterProcessor(uint32_t nodeType) override;
  void registerFactory(uint32_t nodeType, DSP::IDSPNodeFactory* factory) override;

  uint32_t getNodeLatency(NodeID nodeId) const override;
  void setNodeLatency(NodeID nodeId, uint32_t latencySamples) override;
  uint32_t getTotalLatency() const override;
  void applyPDC() override;
  void setPDCCrossfade(uint32_t numSamples) override;

  void process(float *const *inputs, float *const *outputs,
               uint32_t numChannels, uint32_t numSamples,
               const ProcessContext* context) override;

  void setLiveMIDITargets(const NodeID* targets, uint32_t count) override;

  void publishTimelineSnapshot(const TimelineSnapshot& snapshot) override;
  const TimelineSnapshot* getActiveTimelineSnapshot() const override;

  uint32_t getTopologyVersion() const override;
  uint32_t getNodeCount() const override;
  uint32_t getConnectionCount() const override;
  bool hasCycles() const override;

private:
  //==========================================================================
  // Internal State
  //==========================================================================

  // Bridge pointers (not owned)
  Layer2::IMutationBridge *mutationBridge;
  Layer2::ITelemetryBridge *telemetryBridge;
  Layer2::IEventQueue *eventQueue;
  ISidechainManager *sidechainManager = nullptr;

  // Live MIDI target nodes for broadcast routing (RT-safe storage)
  NodeID liveMidiTargets[128];
  uint32_t liveMidiTargetCount;


  // Processor registry (function pointer dispatch)
  ProcessorInfo *processors;
  uint32_t processorCapacity;
  mutable std::mutex processorMutex; // Protects processor registry

  // Factory registry for metadata queries
  std::unordered_map<uint32_t, DSP::IDSPNodeFactory*> factories;

  // Topology buffers (aligned allocation)
  DSPGraphTopology *topologyBuffer0;
  DSPGraphTopology *topologyBuffer1;
  std::atomic<DSPGraphTopology *> activeTopology; // RT reads
  DSPGraphTopology *pendingTopology;              // GUI writes
  std::mutex pendingMutex; // Protects pending topology only

  // Latency tracking
  std::unordered_map<uint64_t, uint32_t> nodeLatencies; // nodeId.toPacked() -> latency
  mutable std::mutex latencyMutex;

  // PDC delay lines (one per connection that needs compensation)
  PDCDelayLine *pdcDelayLines;
  uint32_t pdcCapacity;
  uint32_t pdcCrossfadeSamples; // Default crossfade duration

  // Worker thread for non-RT operations (topological sort, topology growth)
  std::thread workerThread;
  std::atomic<bool> workerRunning;
  std::atomic<bool> workerHasWork;
  Layer2::SPSCQueue<DSPGraphTopology *, 2> workerToRTQueue; // Worker → RT
  Layer2::SPSCQueue<DSPGraphTopology *, 2> rtToWorkerQueue; // RT → Worker
  Layer2::BoundedMPSCQueue<SystemMutation, 1024>
      workerMutationQueue; // GUI → Worker


  std::atomic<uint32_t> globalTopologyVersion;

  // Timeline snapshot quadruple buffering (RCU)
  TimelineSnapshot* snapshotBuffer0_ = nullptr;
  TimelineSnapshot* snapshotBuffer1_ = nullptr;
  TimelineSnapshot* snapshotBuffer2_ = nullptr;
  TimelineSnapshot* snapshotBuffer3_ = nullptr;
  TimelineSnapshot* snapshotBuffers_[4] = {nullptr};
  mutable std::atomic<uint32_t> activeSnapshotIdx_{0};
  mutable std::atomic<uint32_t> previousActiveSnapshotIdx_{0};
  std::atomic<uint32_t> latestSnapshotIdx_{0};

  // Pre-allocated scratch buffers for input separation
  float* scratchInputBuffer = nullptr;
  float* scratchInputPtrs[MAX_SUPPORTED_CHANNELS] = {nullptr};

  //==========================================================================
  // Internal Methods
  //==========================================================================

  // Apply mutation to pending topology (non-RT, called by worker thread)
  void applyMutation(const SystemMutation &mutation);

  // Rebuild execution order using topological sort (non-RT, called by worker
  // thread)
  bool rebuildExecutionOrder(DSPGraphTopology *topology);

  // Worker thread main loop
  void workerThreadFunc();

  // Estimate maximum events needed for a node type
  uint32_t estimateMaxEvents(uint32_t nodeType) const;

  // Allocate expanded topology (non-RT, called when capacity exceeded)
  DSPGraphTopology *allocateExpandedTopology(const DSPGraphTopology *source, uint32_t newCapacity, uint32_t newMaxChannels);

  // Deallocate a topology and all its associated buffers (non-RT)
  void deallocateTopology(DSPGraphTopology *topology);

  // Route output events from a node to downstream nodes (RT-safe)
  void routeOutputEvents(uint32_t sourceNodeIdx, const EventData *events,
                         uint32_t numEvents, uint32_t *nodeEventCounts,
                         uint32_t maxNodes, uint32_t numSamples);

  // Calculate and push telemetry for a node
  void pushNodeTelemetry(NodeID nodeId, const float *const *outputs,
                         uint32_t numChannels, uint32_t numSamples);


  void rebuildRoutingTable(DSPGraphTopology *topology);

  bool wouldExceedCapacity(const SystemMutation &mutation,
                           const DSPGraphTopology *topology) const;

  // Calculate cumulative latencies through graph (non-RT, called by applyPDC)
  uint32_t calculateCumulativeLatency(uint32_t nodeIndex,
                                      const DSPGraphTopology *topology,
                                      uint32_t *outCumulativeLatencies) const;

  // Allocate PDC delay line buffer for a connection
  bool allocatePDCDelayLine(uint32_t pdcIndex, uint32_t maxDelaySamples);
};

} // namespace Layer3
