# Layer 2: Core Infrastructure Services

This layer provides foundational infrastructure for the DAW, including memory management, inter-thread communication, state management, and time representation.

## Components

- **Memory Coordinator** - Priority-aware buffer pool for real-time audio
- **Mutation Bridge** - Lock-free queue for GUI→Audio topology commands (low frequency)
- **Event Queue** - Lock-free queue for high-frequency sample-accurate event delivery (automation, MIDI)
- **Telemetry Bridge** - Lock-free queue for Audio→GUI metering
- **State Manager** - Undo/redo system with snapshot support
- **Tempo Service** - Multi-domain time conversion (samples/beats/BBT)
- **Plugin Scanner** - Crash-isolated plugin discovery

## Threading Model

Layer 2 uses `Layer1::IThreadManager` for all thread creation and priority management:

| Layer 2 Priority | Layer 1 ThreadPriority | Use Case |
|-----------------|------------------------|----------|
| REALTIME | REALTIME | Audio thread (event queue consumer) |
| HIGH | HIGH | DSP worker threads |
| NORMAL | NORMAL | Main/GUI thread (event queue producer) |
| BACKGROUND | LOW | File I/O, plugin scanning |

**Event Queue Flow:**
- **Producers:** Automation threads, GUI thread, MIDI input thread
- **Consumer:** Audio thread (during `process()`)
- **Pattern:** MPSC (Multiple Producer, Single Consumer)

## Dependencies

Layer 2 depends ONLY on:
- System Primitives (`src/common/system_primitives.h`)
- Layer 1: Hardware/OS Abstraction
  - `Layer1::IThreadManager` - Thread creation and priority control
  - `Layer1::ThreadHandle`, `Layer1::ThreadPriority`, `Layer1::RealTimeConstraints`

Layer 2 MUST NOT depend on:
- Layer 3 (Core Audio Engine)
- Layer 4 (DSP Processing Nodes)
- Layer 5 (Musical Composition)
- Layer 6 (Media Management)
- Layer 7 (Presentation)

## Building

Layer 2 is built automatically when building the main project:

```bash
cd build
cmake ..
cmake --build .
```

## Testing

Run unit tests:
```bash
cd build
ctest -R layer2 --output-on-failure
```

## Implementation Status

- [ ] Memory Coordinator
- [ ] Mutation Bridge
- [ ] Event Queue (NEW - for sample-accurate parameter delivery)
- [ ] Telemetry Bridge
- [ ] State Manager
- [ ] Tempo Service
- [ ] Plugin Scanner

## Architecture

Layer 2 implements the following key design principles:

1. **Wait-Free Operations** - All RT-critical operations have bounded execution time
2. **Priority-Aware Allocation** - Memory pool uses tiered allocation to prevent priority inversion
3. **Separation of Concerns** - Mutation Bridge (topology changes) vs Event Queue (parameter events)
4. **Interface-Based Design** - All components expose pure virtual interfaces for replaceability
5. **POD Primitives** - All boundary-crossing structures are Plain Old Data
6. **Layer 1 Integration** - Thread management delegated to Layer 1 abstractions

## References

- [Layer 2 Specification](../../docs/Layer_2_Core_Infrastructure_Services_Spec.md)
- [Layer 2 Implementation Plan](../../docs/Layer_2_Implementation_Plan.md)
- [System Primitives](../../system_primitives_v1.1.h)
