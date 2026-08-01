// src/Core audio engine/scheduler/idsp_kernel.h
#pragma once

#include "system_primitives.h"
#include <cstdint>
#include <memory>

// Forward declarations (Layer 2 interfaces)
namespace Layer2 {
class IMutationBridge;
class ITelemetryBridge;
class IEventQueue;
} // namespace Layer2

namespace DSP { class IDSPNodeFactory; }

namespace Layer3 {
class ISidechainManager;

//==============================================================================
// DSP KERNEL INTERFACE
//==============================================================================

class IDSPKernel {
public:
  //==========================================================================
  // Bridge Attachment (Initialization Only - Not Thread-Safe)
  //==========================================================================

  // Attach mutation bridge for GUI → Audio topology changes
  // Precondition: bridge must outlive kernel
  // Postcondition: Kernel will consume mutations during process()
  // Thread-safety: NOT thread-safe, call during initialization only
  virtual void attachMutationBridge(Layer2::IMutationBridge *bridge) = 0;

  // Attach telemetry bridge for Audio → GUI metering
  // Precondition: bridge must outlive kernel
  // Postcondition: Kernel will push telemetry during process()
  // Thread-safety: NOT thread-safe, call during initialization only
  virtual void attachTelemetryBridge(Layer2::ITelemetryBridge *bridge) = 0;

  // Attach event queue for sample-accurate parameter/MIDI events
  // Precondition: queue must outlive kernel
  // Postcondition: Kernel will pop events during process()
  // Thread-safety: NOT thread-safe, call during initialization only
  virtual void attachEventQueue(Layer2::IEventQueue *eventQueue) = 0;

  // Attach sidechain manager for sidechain buffer routing
  // Precondition: manager must outlive kernel
  // Thread-safety: NOT thread-safe, call during initialization only
  virtual void attachSidechainManager(ISidechainManager *sidechainManager) = 0;

  //==========================================================================
  // Processor Registration (Initialization Only - Not Thread-Safe)
  //==========================================================================

  // Register a processing function for a node type
  // Precondition: nodeType must not already be registered
  // Postcondition: Nodes of this type will call processFunc during process()
  // Thread-safety: NOT thread-safe, call during initialization only
  virtual void registerProcessor(uint32_t nodeType,
                                 DSPProcessFunc processFunc) = 0;

  // Unregister a processing function
  // Precondition: No nodes of this type may exist in graph
  // Thread-safety: NOT thread-safe, call during initialization only
  virtual void unregisterProcessor(uint32_t nodeType) = 0;

  // Register a factory for a node type to support metadata queries (e.g., latency)
  // Precondition: factory must outlive kernel
  virtual void registerFactory(uint32_t nodeType,
                               DSP::IDSPNodeFactory *factory) = 0;

  //==========================================================================
  // Plugin Delay Compensation (RT-Safe)
  //==========================================================================

  // Get reported latency for a specific node
  // Returns: Latency in samples, or 0 if node not found
  // Thread-safety: RT-safe, wait-free (atomic read)
  virtual uint32_t getNodeLatency(NodeID nodeId) const = 0;

  // Set latency for a specific node (called by plugin processor)
  // Precondition: Node must exist in graph
  // Postcondition: PDC will be recalculated on next topology update
  // Thread-safety: RT-safe, wait-free (atomic write)
  virtual void setNodeLatency(NodeID nodeId, uint32_t latencySamples) = 0;

  // Get total graph latency (sum of all latency in signal path)
  // Returns: Total latency in samples
  // Thread-safety: RT-safe, wait-free (atomic read)
  virtual uint32_t getTotalLatency() const = 0;

  // Apply PDC by adding delay compensation to connections
  // Precondition: Latencies must be set for all nodes
  // Postcondition: Delay lines adjusted with crossfade to prevent clicks
  // Thread-safety: RT-safe (crossfades over ~10ms to prevent artifacts)
  virtual void applyPDC() = 0;

  // Set PDC crossfade duration (default: 512 samples at 48kHz)
  // Parameters:
  //   numSamples: Crossfade duration in samples
  // Thread-safety: NOT RT-safe (call during initialization only)
  virtual void setPDCCrossfade(uint32_t numSamples) = 0;

  //==========================================================================
  // Audio Processing (RT-Safe, Wait-Free)
  //==========================================================================

  // Process audio graph
  // Parameters:
  //   inputs: Planar input arrays [channel][sample]
  //   outputs: Planar output arrays [channel][sample]
  //   numChannels: Number of channels (1-32)
  //   numSamples: Buffer size (power of 2 preferred)
  // Algorithm:
  //   1. Check for pre-computed execution order from worker thread
  //   2. Atomic swap with active topology if new order available
  //   3. Pop events from IEventQueue
  //   4. For each node in execution order:
  //      a. Filter events by targetNodeId (using pre-allocated per-node buffer)
  //      b. Call registered processFunc
  //      c. Route output events to downstream nodes
  //      d. Calculate and push telemetry
  // Thread-safety: RT-safe, wait-free (no allocations, no locks)
  // IMPORTANT: Topological sort is computed in non-RT worker thread, NOT here
  virtual void process(float *const *inputs, float *const *outputs,
                       uint32_t numChannels, uint32_t numSamples,
                       const ProcessContext* context) = 0;

  // Set the target node IDs for live (broadcast) MIDI inputs (RT-Safe, Wait-Free)
  virtual void setLiveMIDITargets(const NodeID* targets, uint32_t count) = 0;

  // Timeline snapshot publication and acquisition (RT-Safe, Wait-Free)
  virtual void publishTimelineSnapshot(const TimelineSnapshot& snapshot) = 0;
  virtual const TimelineSnapshot* getActiveTimelineSnapshot() const = 0;


  //==========================================================================
  // Query Operations (RT-Safe, Wait-Free)
  //==========================================================================

  // Get current topology version (increments on each topology change)
  // Thread-safety: RT-safe, wait-free (atomic read)
  virtual uint32_t getTopologyVersion() const = 0;

  // Get number of nodes in graph
  // Thread-safety: RT-safe, wait-free (atomic read)
  virtual uint32_t getNodeCount() const = 0;

  // Get number of connections in graph
  // Thread-safety: RT-safe, wait-free (atomic read)
  virtual uint32_t getConnectionCount() const = 0;

  // Check if graph contains cycles
  // Thread-safety: RT-safe, wait-free (atomic read)
  virtual bool hasCycles() const = 0;

  //==========================================================================
  // Factory
  //==========================================================================

  // Create DSP kernel with specified initial capacity
  // Parameters:
  //   initialNodeCapacity: Maximum number of nodes (must be power of 2)
  // Returns: Unique pointer to kernel instance
  static std::unique_ptr<IDSPKernel> create(uint32_t initialNodeCapacity = 256);

  //==========================================================================
  // Destructor
  //==========================================================================

  virtual ~IDSPKernel() = default;
};

} // namespace Layer3
