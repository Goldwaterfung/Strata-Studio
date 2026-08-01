# Layer 3: Core Audio Engine

This directory contains the core audio engine for the agent-based DAW. It is responsible for graph scheduling, execution, transport control, plugin hosting, and streaming.

## Subsystems

- **Scheduler**: Handles the DSP graph topology and RT-safe execution order.
- **Transport**: Manages playback state, position, and tempo synchronization.
- **Plugin**: Lifecycle management and processing for VST3, AU, and CLAP plugins.
- **Streaming**: Butler thread coordination for disk I/O and streaming buffers.
- **Automation**: Sample-accurate automation processing.
- **Sidechain**: Routing and management for sidechain inputs.
- **Control**: Mapping and feedback for MIDI/HID control surfaces.

## Architecture

The engine follows strict real-time safety principles:
1. **No Memory Allocation** on the audio thread.
2. **Lock-Free/Wait-Free** communication between threads.
3. **Double-Buffered Topology** for seamless graph updates.
4. **Worker Thread Offloading** for heavy non-RT operations (topological sorting, capacity growth).
