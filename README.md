<div align="center">

# <img src="asset/strata.png" alt="Strata" width="512" style="vertical-align: middle;" /> Strata Studio

### Agentic & Modular Digital Audio Workstation

<p align="center">
  <b>A next-generation human-agent collaborative DAW designed for intelligent music production, sound design, and audio engineering.</b>
</p>

[![License: CC BY 4.0](https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg)](http://creativecommons.org/licenses/by/4.0/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.20%2B-green.svg)](https://cmake.org/)

---

<p align="center">
  <a href="#-key-features">Key Features</a> •
  <a href="#-project-overview--architecture">Architecture</a> •
  <a href="#-building--installation">Building & Installation</a> •
  <a href="#-agentic-cli-command-reference-daw-cli">CLI Reference</a> •
  <a href="#-development--roadmap">Roadmap</a> •
  <a href="#-license">License</a>
</p>

</div>

---

## 💡 Overview

**Strata Studio** is a collaborative human-agent Digital Audio Workstation (DAW) designed to elevate music production. It pairs creative artists with intelligent AI agents, allowing producers, sound designers, and engineers to co-create, mix, edit, and automate music through seamless human-AI partnership alongside classic DAW tools.

---

## ✨ Key Features

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <h3>⚡ Agentic Co-Creation</h3>
      <p>Pair human creative intuition with agentic precision—allowing intelligent agents to handle tedious edits, track routing, automation curves, and spectral feedback.</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 Universal Plugin Support</h3>
      <p>Run your favorite virtual instruments and audio effects with full support for <b>VST3</b>, <b>Audio Units (AU)</b>, and <b>CLAP</b> plugin formats.</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎛️ Complete Production Suite</h3>
      <p>Full multitrack recording, intuitive Piano Roll MIDI editing, dynamic tempo maps, non-destructive comping, and high-quality stem exports.</p>
    </td>
    <td width="50%" valign="top">
      <h3>🚀 Real-Time Performance</h3>
      <p>Engineered for rock-solid stability and low-latency audio processing, ensuring your studio sessions run smoothly without dropouts or buffer lags.</p>
    </td>
  </tr>
</table>

---

## 🏗️ Project Overview & Architecture

Strata Studio is engineered around a strict **8-layer architecture** to guarantee real-time safety, modular maintainability, and deterministic high-performance audio execution:

- ⚡ **Real-Time Safe Audio Engine**
- 🔀 **Deterministic DSP Graph Topology**
- 🧱 **Decoupled 8-Layer Hierarchy**
- 🤖 **Agentic IPC & Automation Daemon (`daw-cli`)**
- 🔌 **Multi-Format Host Infrastructure**

### 8-Layer Architecture Hierarchy

<table width="100%">
  <thead>
    <tr>
      <th align="center">Layer</th>
      <th align="left">Layer Name</th>
      <th align="left">Key Responsibility</th>
      <th align="left">Core Directory</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="center"><b>Layer 8</b></td>
      <td><b>Agentic Layer</b></td>
      <td>CLI Parser (<code>daw-cli</code>), IPC Server Daemon for AI Agents</td>
      <td><code>src/Agentic layer/</code></td>
    </tr>
    <tr>
      <td align="center"><b>Layer 7</b></td>
      <td><b>Presentation</b></td>
      <td>UI Views, Piano Roll, Playlist, Widgets, Settings</td>
      <td><code>src/Presentation/</code></td>
    </tr>
    <tr>
      <td align="center"><b>Bridge</b></td>
      <td><b>Middle Bridge</b></td>
      <td>Decoupled UI & Agent Facade Controllers (<code>namespace bridge</code>)</td>
      <td><code>src/Middle Bridge/</code></td>
    </tr>
    <tr>
      <td align="center"><b>Layer 6</b></td>
      <td><b>Media Management</b></td>
      <td>Library, Waveform Generation, Peak Caching, Codecs</td>
      <td><code>src/Media management/</code></td>
    </tr>
    <tr>
      <td align="center"><b>Layer 5</b></td>
      <td><b>Musical Composition</b></td>
      <td>Arranger, Tracks, MIDI Sequencing, Comping, Tempo Maps</td>
      <td><code>src/Musical composition/</code></td>
    </tr>
    <tr>
      <td align="center"><b>Layer 4</b></td>
      <td><b>DSP Processing Nodes</b></td>
      <td>Zero-allocation Channel Strips, Panners, Time/Pitch</td>
      <td><code>src/DSP nodes/</code></td>
    </tr>
    <tr>
      <td align="center"><b>Layer 3</b></td>
      <td><b>Core Audio Engine</b></td>
      <td>Transport, Scheduler, Audio Streaming, Plugin Hosting</td>
      <td><code>src/Core audio engine/</code></td>
    </tr>
    <tr>
      <td align="center"><b>Layer 2</b></td>
      <td><b>Core Infrastructure</b></td>
      <td>Lock-Free Ring Buffers, State Manager, Telemetry/Mutation Bridges</td>
      <td><code>src/Core infrastructure/</code></td>
    </tr>
    <tr>
      <td align="center"><b>Layer 1</b></td>
      <td><b>Hardware/OS Abstraction</b></td>
      <td>Audio HAL (RtAudio), MIDI HAL (RtMidi), File I/O</td>
      <td><code>src/Hardware/OS abstraction/</code></td>
    </tr>
  </tbody>
</table>

---

## 🛠️ Building & Installation

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

---

### Build Instructions

1. **Clone the repository** (if not already done):
   ```bash
   git clone https://github.com/Goldwaterfung/Strata-Studio.git
   cd Strata-Studio
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

---

### Build Options

<table width="100%">
  <thead>
    <tr>
      <th align="left">Option</th>
      <th align="center">Default</th>
      <th align="left">Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>BUILD_TESTS</code></td>
      <td align="center"><code>OFF</code></td>
      <td>Build unit tests</td>
    </tr>
    <tr>
      <td><code>BUILD_PERFORMANCE_TESTS</code></td>
      <td align="center"><code>OFF</code></td>
      <td>Build performance benchmarks</td>
    </tr>
    <tr>
      <td><code>ENABLE_SIMD</code></td>
      <td align="center"><code>ON</code></td>
      <td>Enable SIMD optimizations (AVX2)</td>
    </tr>
    <tr>
      <td><code>USE_ASAN</code></td>
      <td align="center"><code>OFF</code></td>
      <td>Enable Address Sanitizer</td>
    </tr>
    <tr>
      <td><code>USE_TSAN</code></td>
      <td align="center"><code>OFF</code></td>
      <td>Enable Thread Sanitizer</td>
    </tr>
    <tr>
      <td><code>BUILD_PLUGINS</code></td>
      <td align="center"><code>ON</code></td>
      <td>Build plugin host support</td>
    </tr>
  </tbody>
</table>

Example configuring with custom options:
```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_PERFORMANCE_TESTS=ON \
      -DENABLE_SIMD=ON \
      ../..
```

---

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

---

### Release Builds

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <b>Standard Release:</b>
      <pre><code>./scripts/build.sh release</code></pre>
    </td>
    <td width="50%" valign="top">
      <b>Packaged Release:</b>
      <pre><code>./scripts/build.sh release --package</code></pre>
    </td>
  </tr>
</table>

---

## 🤖 Agentic CLI Command Reference (`daw-cli`)

Strata Studio features an Agentic IPC daemon and command-line utility (`daw-cli`) designed for AI agents and automated control.

```bash
# Executable binary location
./build/release/src/Agentic\ layer/daw-cli [command] [options]
```

<details open>
<summary><b>1. Session State & Transport <code>[Implemented]</code></b></summary>
<br>

- `daw-cli status` — Returns session state, tempo, time signature, and playhead position.
- `daw-cli transport play` — Starts transport audio playback.
- `daw-cli transport stop` — Stops transport audio playback.
- `daw-cli transport set-tempo --bpm 128.0` — Sets session tempo in BPM.
- `daw-cli transport set-time-signature --num 4 --den 4` — Sets time signature numerator and denominator.
</details>

<br>

<details open>
<summary><b>2. Tracks & Gain-Staging <code>[Implemented]</code></b></summary>
<br>

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
</details>

<br>

<details open>
<summary><b>3. VST3 / AU Plugin Host Management <code>[Implemented]</code></b></summary>
<br>

- `daw-cli plugin scan` — Scans host system for installed VST3/AU plugins.
- `daw-cli plugin list --head 10 --filter "FabFilter" --category effect` — Lists discovered plugins.
- `daw-cli plugin add --track 1 --name "FabFilter Pro-Q 3"` — Inserts a plugin into a track slot.
- `daw-cli plugin set-param --track 1 --plugin 0 --param 3 --val 0.75` — Sets a plugin parameter value (`0.0`..`1.0`).
- `daw-cli plugin copy --from-track 1 --slot 0 --to-tracks 2..5 --overwrite` — Copies a plugin slot to target track(s).
- `daw-cli plugin copy-chain --from-track 1 --to-tracks 2..5 --overwrite` — Copies entire 8-slot insert chain to target tracks.
</details>

---

## 🗺️ Development & Roadmap

### Agentic Layer (`daw-cli`) Feature Status

- [x] **Session State & Transport** (`status`, `transport`) - Fully Implemented
- [x] **Tracks & Gain-Staging** (`track`, `prep`) - Fully Implemented
- [x] **VST3 / AU Plugin Host Management** (`plugin`) - Fully Implemented
- [ ] **Clips & Timeline Editing** (`clip`, `midi`) - *In Progress*
- [ ] **Bus Submixing & Auxiliary FX Routing** (`route`) - *In Progress*
- [ ] **Non-Visual DSP Analysis & Audio Intelligence** (`analyze`) - *In Progress*
- [ ] **Stem Exports & Asynchronous Render Jobs** (`export`, `job`) - *In Progress*

---

## 📜 License

<div align="center">

[![CC BY 4.0][cc-by-shield]][cc-by]

This work is licensed under a  
[Creative Commons Attribution 4.0 International License][cc-by].

[![CC BY 4.0][cc-by-image]][cc-by]

[cc-by]: http://creativecommons.org/licenses/by/4.0/
[cc-by-image]: https://i.creativecommons.org/l/by/4.0/88x31.png
[cc-by-shield]: https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg

</div>

---

## 🙏 Acknowledgments

<div align="center">

Architecture inspired by:  
**[Ardour](https://ardour.org/)** (libardour) • **[Bitwig Studio](https://www.bitwig.com/)** • **[Reaper](https://www.reaper.fm/)** • **[JUCE Framework](https://juce.com/)**

</div>

