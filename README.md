<div align="center">

# ![Strata](asset/strata.png) Strata Studio

### Give your AI agent hands in your DAW.

<p align="center">
  <b>A next-generation human-agent collaborative Digital Audio Workstation (DAW).<br>Co-create, mix, edit, and automate music by talking to your favorite AI agent.</b>
</p>

[![License: CC BY 4.0](https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg)](http://creativecommons.org/licenses/by/4.0/)
[![Agent Skills](https://img.shields.io/badge/Agent%20Skills-Standard-green)](https://agentskills.io)
[![Multi-Runtime](https://img.shields.io/badge/Runtime-Claude%20Code%20·%20Codex%20·%20Cursor%20·%20Hermes%20·%20Gemini-blueviolet)](#quick-start--agentic-install)
[![Plugin Host](https://img.shields.io/badge/Plugins-VST3%20%7C%20AU%20%7C%20CLAP-blue.svg)](#key-features)

---

<p align="center">
  <a href="#quick-start--agentic-install">Quick Start</a> •
  <a href="#examples">Examples</a> •
  <a href="#key-features">Key Features</a> •
  <a href="#agentic-cli-command-reference-daw-cli">CLI Reference</a> •
  <a href="#architecture--developer-guide">Developer Guide</a> •
  <a href="#license">License</a>
</p>

</div>

---

## Overview

**Strata Studio** is an open-source, human-agent collaborative DAW designed for creators, sound designers, and producers. 

Instead of spending hours clicking through menus to gain-stage tracks, map automation curves, trim clip silence, or set up plugin chains, you can **pair-program your session directly with your AI agent** while maintaining full creative control over your music.

---

## Quick Start & Agentic Install

Strata Studio ships with a pre-packaged agent skill ([`skills/daw-cli/`](skills/daw-cli/)) built on the open [Agent Skills](https://agentskills.io) standard.

### Option 1: One-line install (recommended)

Open your skills-compatible agent — **Claude Code**, **Codex**, **Cursor**, **Hermes**, **Gemini CLI**, **OpenCode**, and 50+ more — and tell it:

```text
Install this skill for me: https://github.com/Goldwaterfung/Strata-Studio
```

Or use the universal CLI installer ([vercel-labs/skills](https://github.com/vercel-labs/skills)):

```bash
npx skills add Goldwaterfung/Strata-Studio
```

It auto-detects your runtime and installs the `daw-cli` skill instruction set so your agent immediately learns how to control Strata Studio.

<details>
<summary><b>Option 2: Manual Skill Directory Setup</b></summary>
<br>

To manually install the skill into your preferred AI agent framework, copy or symlink the `skills/daw-cli/` directory:

| Agent Framework | Local Workspace Skill Path | Global User Skill Path |
| :--- | :--- | :--- |
| **Codex** | `.agents/skills/daw-cli` | `~/.agents/skills/daw-cli` |
| **Claude Code / Co-Work** | `.claude/skills/daw-cli` | `~/.claude/skills/daw-cli` |
| **Hermes** | `.hermes/skills/daw-cli` | `~/.hermes/skills/daw-cli` |
| **Antigravity** | `.agents/skills/daw-cli` | `~/.gemini/config/skills/daw-cli` |
| **Gemini CLI** | `.gemini/skills/daw-cli` | `~/.gemini/skills/daw-cli` |
| **OpenCode** | `.opencode/skills/daw-cli` | `~/.config/opencode/skills/daw-cli` |

Once installed, your agent uses [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) as its operational manual.

</details>

---

## Examples

Here is what working in Strata Studio with an AI assistant looks like:

```text
User    ❯ Set session tempo to 128 BPM, batch create audio tracks for Kick, Snare, HH, and Tom, and gain-stage them to -18 dB RMS.

Agent   ❯ [Strata Agentic Engine]
          ✓ Set session tempo to 128.0 BPM (4/4 time signature)
          ✓ Batch created 4 audio tracks: Kick, Snare, HH, Tom
          ✓ Normalized & gain-staged clips across tracks 1..4 to target -18.0 dB RMS
          Done. Ready for your arrangement pass.
```

```text
User    ❯ Add a FabFilter Pro-Q 3 equalizer to the Snare track and copy the chain to the Tom tracks.

Agent   ❯ [Strata Agentic Engine]
          ✓ Scanned VST3/AU host plugins
          ✓ Inserted 'FabFilter Pro-Q 3' into slot 0 on Track 2 (Snare)
          ✓ Copied insert chain from Track 2 to Tracks 3..4
```

---

## Key Features

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <h3>🤖 Hands-Free Studio Assistant</h3>
      <p>Offload tedious engineering work—handling repetitive clip edits, multi-bus gain staging, track auto-coloring, and silence trimming via natural language prompts to your AI agent.</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 Universal Plugin Support</h3>
      <p>Host your favorite virtual instruments and audio effects with full support for <b>VST3</b>, <b>Audio Units (AU)</b>, and <b>CLAP</b> plugin formats.</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎛️ Complete Production Suite</h3>
      <p>Full multitrack audio recording, intuitive Piano Roll MIDI editing, dynamic tempo maps, non-destructive clip comping, and high-quality stem exports.</p>
    </td>
    <td width="50%" valign="top">
      <h3>⚡ Real-Time Engine Performance</h3>
      <p>Built from the ground up in modern C++20 with a lock-free, zero-allocation DSP execution thread ensuring rock-solid low-latency performance with zero dropouts.</p>
    </td>
  </tr>
</table>

---

## Agentic CLI Command Reference (`daw-cli`)

Strata Studio features an Agentic IPC daemon and command-line utility (`daw-cli`) designed for AI agents and automated session control.

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

<br>

<details open>
<summary><b>4. Clips & Timeline Editing <code>[Implemented]</code></b></summary>
<br>

- `daw-cli clip add-audio --track 1 --path "/audio/vocal.wav" --start 1.1.0` — Imports an audio clip onto a track timeline.
- `daw-cli clip add-midi --track 1 --start 1.1.0 --dur 4.0.0` — Inserts a new MIDI clip region on a track.
- `daw-cli midi add-note --track 1 --clip 1 --pitch C4 --velocity 100 --start 1.1.0 --dur 1.0.0` — Adds a MIDI note event to a clip.
- `daw-cli clip list --track 1` — Lists all arrangement clips with start bar, duration, gain, and mute status.
- `daw-cli clip set-gain --track 1 --clip 1 --db -3.0` — Sets clip region volume gain in dB.
- `daw-cli clip set-mute --track 1 --clip 1 --on true` — Mutes or unmutes a specific clip.
- `daw-cli clip split --track 1 --clip 1 --at 2.1.0` — Splits a clip into two regions at position.
- `daw-cli clip trim-silence --track 1 --clip 1 --threshold -48.0 --fade-ms 5.0` — Trims lead/tail silence on clip.
- `daw-cli clip quantize --track 1 --clip 1 --grid 1/16 --strength 1.0` — Quantizes notes/clip positions to grid.
- `daw-cli clip merge --track 1 --start 1.1.0 --end 5.1.0` — Merges adjacent clips across a timeline range.
- `daw-cli clip move --track 1 --clip 1 --to-pos 3.1.0` — Relocates a clip to a target bar/position.
- `daw-cli clip nudge --track 1 --clip 1 --by +1/16` — Nudges clip position by grid fraction.
</details>

---

## Architecture & Developer Guide

<details>
<summary><b>Engine Architecture (8-Layer Hierarchy)</b></summary>
<br>

Strata Studio is engineered in C++20 around a strict **8-layer architecture** to guarantee real-time safety, modular maintainability, and deterministic DSP execution:

- **Real-Time Safe Audio Engine** (Zero heap allocations on DSP audio thread)
- **Deterministic DSP Graph Topology**
- **Decoupled 8-Layer Hierarchy**
- **Agentic IPC & Automation Daemon (`daw-cli`)**
- **Multi-Format Host Infrastructure**

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

</details>

<details>
<summary><b>Building & Compiling from Source</b></summary>
<br>

### Prerequisites

The project uses **vcpkg** in manifest mode to manage dependencies.

#### Required Tools
- **CMake** 3.20 or higher
- **Git**
- **C++20 compatible compiler**: Clang 12+, GCC 11+, or MSVC 2022+

#### Automatic Setup
Run the setup script to install dependencies and libraries (RtAudio, RtMidi, libsndfile, nlohmann_json, spdlog, Catch2):

```bash
./scripts/install_dependencies.sh
```

---

### Build Instructions

1. **Clone the repository**:
   ```bash
   git clone https://github.com/Goldwaterfung/Strata-Studio.git
   cd Strata-Studio
   ```

2. **Configure and build**:
   ```bash
   mkdir -p build/debug && cd build/debug
   cmake -DCMAKE_BUILD_TYPE=Debug ../../
   cmake --build . --parallel
   ```

3. **Run application**:
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

---

### Testing & Release Builds

```bash
# Build & run unit tests
./scripts/build.sh debug --test

# Build release binary
./scripts/build.sh release

# Packaged release
./scripts/build.sh release --package
```

</details>

---

## Development & Roadmap

### Agentic Layer (`daw-cli`) Feature Status

- [x] **Session State & Transport** (`status`, `transport`) - Fully Implemented
- [x] **Tracks & Gain-Staging** (`track`, `prep`) - Fully Implemented
- [x] **VST3 / AU Plugin Host Management** (`plugin`) - Fully Implemented
- [x] **Clips & Timeline Editing** (`clip`, `midi`) - Fully Implemented
- [ ] **Bus Submixing & Auxiliary FX Routing** (`route`) - *In Progress*
- [ ] **Non-Visual DSP Analysis & Audio Intelligence** (`analyze`) - *In Progress*
- [ ] **Stem Exports & Asynchronous Render Jobs** (`export`, `job`) - *In Progress*

---

## License

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

## Acknowledgments

<div align="center">

Architecture inspired by:  
**[Ardour](https://ardour.org/)** (libardour) • **[Bitwig Studio](https://www.bitwig.com/)** • **[Reaper](https://www.reaper.fm/)** • **[JUCE Framework](https://juce.com/)**

</div>


