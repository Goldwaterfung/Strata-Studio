# Strata Studio — Agentic & Modular Digital Audio Workstation

**Strata Studio** is a collaborative human-agent Digital Audio Workstation (DAW) designed to elevate music production. It pairs creative artists with intelligent AI agents, allowing producers, sound designers, and engineers to co-create, mix, edit, and automate music through seamless human-AI partnership alongside classic DAW tools.

---

## Key Features

- **Human-AI Co-Creation & Smart Control**: Pair human creative intuition with agentic precision—allowing intelligent agents to handle tedious edits, track routing, automation curves, and spectral feedback.
- **Universal Plugin Compatibility**: Run your favorite virtual instruments and audio effects with full support for **VST3**, **Audio Units (AU)**, and **CLAP** plugin formats.
- **Complete Production & Mixing Suite**: Full multitrack recording, intuitive Piano Roll MIDI editing, dynamic tempo maps, non-destructive comping, and high-quality stem exports.
- **Ultra-Fast & Reliable Performance**: Engineered for rock-solid stability and low-latency audio processing, ensuring your studio sessions run smoothly without dropouts or buffer lags.

---

## Project Overview & Architecture

Strata Studio is engineered around a strict 8-layer architecture to guarantee real-time safety, modular maintainability, and deterministic high-performance audio execution:

- **Real-Time Safe Audio Engine**
- **Deterministic DSP Graph Topology**
- **Decoupled 8-Layer Hierarchy**
- **Agentic IPC & Automation Daemon (`daw-cli`)**
- **Multi-Format Host Infrastructure**

### 8-Layer Architecture Hierarchy

| Layer | Layer Name | Key Responsibility | Core Directory |
| :--- | :--- | :--- | :--- |
| **Layer 8** | Agentic Layer | CLI Parser (`daw-cli`), IPC Server Daemon for AI Agents | `src/Agentic layer/` |
| **Layer 7** | Presentation | UI Views, Piano Roll, Playlist, Widgets, Settings | `src/Presentation/` |
| **Bridge** | Middle Bridge | Decoupled UI & Agent Facade Controllers (`namespace bridge`) | `src/Middle Bridge/` |
| **Layer 6** | Media Management | Library, Waveform Generation, Peak Caching, Codecs | `src/Media management/` |
| **Layer 5** | Musical Composition | Arranger, Tracks, MIDI Sequencing, Comping, Tempo Maps | `src/Musical composition/` |
| **Layer 4** | DSP Processing Nodes | Zero-allocation Channel Strips, Panners, Time/Pitch | `src/DSP nodes/` |
| **Layer 3** | Core Audio Engine | Transport, Scheduler, Audio Streaming, Plugin Hosting | `src/Core audio engine/` |
| **Layer 2** | Core Infrastructure | Lock-Free Ring Buffers, State Manager, Telemetry/Mutation Bridges | `src/Core infrastructure/` |
| **Layer 1** | Hardware/OS Abstraction | Audio HAL (RtAudio), MIDI HAL (RtMidi), File I/O | `src/Hardware/OS abstraction/` |

## Building

### Prerequisites

The project uses **vcpkg** in manifest mode to manage dependencies. This ensures consistent versions across all platforms.

#### Required Tools
- **CMake** 3.20 or higher
- **Git**
- **C++20 compatible compiler**:
  - Clang 12+
  - GCC 11+
  - MSVC 2022+

#### Automatic Setup (Recommended)
Run the following script to install build tools and setup vcpkg with all libraries:
```bash
./scripts/install_dependencies.sh
```

This will handle:
- **RtAudio** 6.x
- **RtMidi** 5.x
- **libsndfile** 1.x
- **nlohmann/json** 3.x
- **spdlog** 1.x
- **Catch2** 3.x

### Build Instructions

1. **Clone the repository** (if not already done):
```bash
git clone <repository-url>
cd agent-based-daw
```

2. **Configure the build**:
```bash
mkdir -p build/debug
cd build/debug
cmake -DCMAKE_BUILD_TYPE=Debug ../../
```

3. **Build the project**:
```bash
cmake --build . --parallel $(sysctl -n hw.ncpu)  # macOS
cmake --build . --parallel                       # Windows
```

4. **Run tests** (optional):
```bash
ctest --output-on-failure
```

5. **Run the application**:
```bash
./bin/strata_studio
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | OFF | Build unit tests |
| `BUILD_PERFORMANCE_TESTS` | OFF | Build performance benchmarks |
| `ENABLE_SIMD` | ON | Enable SIMD optimizations (AVX2) |
| `USE_ASAN` | OFF | Enable Address Sanitizer |
| `USE_TSAN` | OFF | Enable Thread Sanitizer |
| `BUILD_PLUGINS` | ON | Build plugin host support |

Example with options:
```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_PERFORMANCE_TESTS=ON \
      -DENABLE_SIMD=ON \
      ../..
```

## Agentic CLI Command Reference (`daw-cli`)

Strata Studio features an Agentic IPC daemon and command-line utility (`daw-cli`) designed for AI agents and automated control.

```bash
# Executable binary location
./build/release/src/Agentic\ layer/daw-cli [command] [options]
```

### 1. Session State & Transport `[Implemented]`
- `daw-cli status` — Returns session state, tempo, time signature, and playhead position.
- `daw-cli transport play` — Starts transport audio playback.
- `daw-cli transport stop` — Stops transport audio playback.
- `daw-cli transport set-tempo --bpm 128.0` — Sets session tempo in BPM.
- `daw-cli transport set-time-signature --num 4 --den 4` — Sets time signature numerator and denominator.

### 2. Tracks & Gain-Staging `[Implemented]`
- `daw-cli track create --type audio --name "Vocal Lead"` — Creates a new track (`audio`, `midi`, `instrument`, `aux`, `folder`).
- `daw-cli track create-batch --type audio --names "Kick,Snare,HH,Tom"` — Batch creates multiple tracks simultaneously.
- `daw-cli track list` — Lists all tracks in the project (TSV tabular output by default).
- `daw-cli track inspect --track 1` — Inspects detailed settings for a single track.
- `daw-cli track set-gain --track 1..4 --db -3.5` — Sets track volume gain in dB.
- `daw-cli track set-pan --track 1 --value -0.25` — Sets track pan position (`-1.0` Left to `+1.0` Right).
- `daw-cli track set-mute --track 2 --on` — Sets mute state on track(s).
- `daw-cli track set-solo --track 2 --on` — Sets solo state on track(s).
- `daw-cli track delete --track 4` — Removes track(s) from the project.
- `daw-cli track sanitize-names` — Auto-cleans stem filenames across project tracks.
- `daw-cli track auto-color` — Assigns semantic colors based on instrument family.
- `daw-cli track set-color --track 1..4 --color red` — Sets track color by semantic name or Hex string.
- `daw-cli prep gain-stage --track 1..8 --target-rms -18.0` — Gain-stages clips on track(s) to target RMS headroom.

### 3. VST3 / AU Plugin Host Management `[Implemented]`
- `daw-cli plugin scan` — Scans host system for installed VST3/AU plugins.
- `daw-cli plugin list --head 10 --filter "FabFilter" --category effect` — Lists discovered plugins.
- `daw-cli plugin add --track 1 --name "FabFilter Pro-Q 3"` — Inserts a plugin into a track slot.
- `daw-cli plugin set-param --track 1 --plugin 0 --param 3 --val 0.75` — Sets a plugin parameter value (`0.0`..`1.0`).
- `daw-cli plugin copy --from-track 1 --slot 0 --to-tracks 2..5 --overwrite` — Copies a plugin slot to target track(s).
- `daw-cli plugin copy-chain --from-track 1 --to-tracks 2..5 --overwrite` — Copies entire 8-slot insert chain to target tracks.

## Build

### Testing

The recommended way to build and run the test suite is using the included build script:

```bash
# Build and run unit tests
./scripts/build.sh debug --test
```

Alternatively, configure and run tests manually with CMake:

```bash
# 1. Configure CMake with unit tests enabled (-DBUILD_TESTS=ON)
mkdir -p build/debug && cd build/debug
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON ../..

# 2. Build test targets
cmake --build .

# 3. Execute test suite via CTest
ctest --output-on-failure
```

### Complete release build

**Without packages:**

```bash
./scripts/build.sh release
```

**With packages:**
```bash
./scripts/build.sh release --package
```

## Development

## Roadmap

### Agentic Layer (`daw-cli`) Feature Status

- [x] **Session State & Transport** (`status`, `transport`) - Fully Implemented
- [x] **Tracks & Gain-Staging** (`track`, `prep`) - Fully Implemented
- [x] **VST3 / AU Plugin Host Management** (`plugin`) - Fully Implemented
- [ ] **Clips & Timeline Editing** (`clip`, `midi`) - *In Progress*
  - [ ] `clip add-audio`, `clip split`, `clip trim-silence`, `clip quantize`, `clip merge`, `clip move`, `clip nudge`, `clip set-gain`, `clip list`
  - [ ] `midi add-note`
- [ ] **Bus Submixing & Auxiliary FX Routing** (`route`) - *In Progress*
  - [ ] `route folder`, `route send`, `route sidechain`, `route list`
- [ ] **Non-Visual DSP Analysis & Audio Intelligence** (`analyze`) - *In Progress*
  - [ ] `analyze spectrum`, `analyze resonance`, `analyze masking`, `analyze loudness`, `analyze true-peak`, `analyze phase-matrix`, `analyze phase-align`, `analyze stereo-width`
- [ ] **Stem Exports & Asynchronous Render Jobs** (`export`, `job`) - *In Progress*
  - [ ] `export stems`
  - [ ] `job status`, `job cancel`, `job list`

## License

Shield: [![CC BY 4.0][cc-by-shield]][cc-by]

This work is licensed under a
[Creative Commons Attribution 4.0 International License][cc-by].

[![CC BY 4.0][cc-by-image]][cc-by]

[cc-by]: http://creativecommons.org/licenses/by/4.0/
[cc-by-image]: https://i.creativecommons.org/l/by/4.0/88x31.png
[cc-by-shield]: https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg

## Acknowledgments

Architecture inspired by:
- Ardour (libardour)
- Bitwig Studio
- Reaper
- JUCE Framework
