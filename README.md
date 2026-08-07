<div align="center">

# ![Strata](asset/strata.png) Strata Studio

### Strata Studio

<p align="center">
  <b>Get your session mixed, balanced, and organized in seconds.<br>Stop wasting time on tedious DAW setup—tell your AI assistant what you need and stay in your creative flow.</b>
</p>

[![License: CC BY 4.0](https://img.shields.io/badge/License-CC%20BY%204.0-lightgrey.svg)](http://creativecommons.org/licenses/by/4.0/)
[![Agent Skills](https://img.shields.io/badge/Agent%20Skills-Standard-green)](https://agentskills.io)
[![Multi-Runtime](https://img.shields.io/badge/Runtime-Claude%20Code%20·%20Codex%20·%20Cursor%20·%20Hermes%20·%20Gemini-blueviolet)](#quick-start--agentic-install)
[![Plugin Host](https://img.shields.io/badge/Plugins-VST3%20%7C%20AU%20%7C%20CLAP-blue.svg)](#key-features)

---

<p align="center">
  <a href="#why-an-agentic-daw">Why Agentic DAW</a> •
  <a href="#quick-start--agentic-setup">Quick Start</a> •
  <a href="#showcase-use-cases">Showcase Use Cases</a> •
  <a href="#what-your-ai-agent-can-do-in-strata-studio">Agent Capabilities</a> •
  <a href="#key-features">Key Features</a> •
  <a href="#developer-guide--building-from-source">Developer Guide</a> •
  <a href="#license">License</a>
</p>

<p align="center">
  <b>Other Languages:</b><br>
  <a href="README_ZH.md">简体中文</a> •
  <a href="README_ZH_TW.md">繁體中文</a> •
  <a href="README_JA.md">日本語</a> •
  <a href="README_KO.md">한국어</a>
</p>

</div>

---

## Overview

You opened your DAW to create music—not to burn half your brain on shortcuts, modifier keys, and complex control interfaces. **Let tools serve your creativity, not your memory.**

**Strata Studio** is an agentic DAW that your AI agent can natively control so you can focus purely on making music. Instead of spending 30 minutes manually levelling tracks for streaming, removing background noise from recordings, or routing effect plugins across every track, just tell your AI agent (**Claude Code**, **Cursor**, **Codex**, **Hermes**, or **Gemini**) what you want in plain English. Your session gets prepped, balanced, and ready for production in seconds.

---

## Why an Agentic DAW?

Traditional DAWs were built for a keyboard-and-mouse era where every single operation required manual labor. As sessions grow to 100+ tracks, your creative energy gets drained by repetitive mechanical tasks.

| Traditional DAW Workflow | Strata Studio Agentic Workflow |
| :--- | :--- |
| **Hundreds of Clicks & Manual Edits**: Manually selecting, gain-staging, color-coding, and routing 100 tracks one-by-one. | **One Single Command**: A single natural language prompt controls 100+ tracks deterministically in a split second. |
| **The Shortcut Tax**: Searching online for forgotten keyboard shortcuts and complex modifier keys. | **Natural Language**: Just tell your AI agent what you want in plain English. |
| **Dumb Track Labels**: DAWs treat track names as plain text strings, forcing you to manually group and bus them. | **Semantic Intelligence**: AI agents understand instrument context (`GTR_L_57` → Guitar Bus) and group stems automatically. |
| **Creative Interruption**: Stopping your flow state to handle technical preferences, gain staging, and file prep. | **Pure Creative Flow**: The AI agent works in the background as your tireless studio assistant. |

---

## Quick Start & Agentic Setup

No terminal commands or manual compilation required. Open your AI agent (**Claude Code**, **Codex**, **Cursor**, **Hermes**, **Gemini CLI**, **OpenCode**, and 50+ more) and tell it to handle the setup:

### 1. Clone the Repository
Clone the project repository to your local system:

```bash
git clone https://github.com/Goldwaterfung/Strata-Studio.git
cd Strata-Studio
```

Or tell your AI agent:

```text
Clone https://github.com/Goldwaterfung/Strata-Studio and set up the project for me
```

### 2. Build & Setup Strata Studio (Agent compiles the app)
Tell your agent:

```text
Build and package Strata Studio for me
```

*(Your agent will automatically run `./scripts/install_dependencies.sh` and `./scripts/build.sh release --package` under the hood).*

### 3. Install the Skill (Teaches your agent how to control DAW)
Tell your agent:

```text
Install the daw-cli skill from https://github.com/Goldwaterfung/Strata-Studio
```

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

## Showcase Use Cases

Below are real-time demonstrations of `daw-cli` executing agentic workflows in Strata Studio.

### 🎬 Use Case 1: Instant Multi-Track Ingestion, Clip Placement & Auto-Coloring

> **Natural Language Prompt:**  
> *"Import all the audio files in `/Volumes/AudioDrives/Stems/Song_Session_A/`. Place all clips at the start of the timeline. Clean up all the dirty file extensions and underscores in the track names, and auto-color the tracks based on their instrument types so my session is clean and organized."*

![Use Case 1: Multi-Track Ingestion & Auto-Coloring](asset/usecase-1.gif)

* **Key Commands:** `daw-cli track create`, `daw-cli clip add-audio`, `daw-cli track sanitize-names`, `daw-cli track auto-color`

---

### 🎬 Use Case 2: Multi-Track Session Orchestration & Fader Layout

> **Natural Language Prompt:**  
> *"Set up a 124 BPM session with an 8-piece drum kit: Kick, Snare Top, Snare Bottom, Hi-Hat, Tom 1, Tom 2, and stereo Overheads. Color them automatically, set all faders to -6 dB for headroom, and pan the overheads 100% left and right."*

![Use Case 2: Multi-Track Session Orchestration & Fader Layout](asset/usecase-2.gif)

* **Key Commands:** `daw-cli transport set-tempo`, `daw-cli track create-batch`, `daw-cli track auto-color`, `daw-cli track set-gain`, `daw-cli track set-pan`

---

### 🎬 Use Case 3: Selective Stem Ingestion, Synth Plugin Instantiation & Submix Routing

> **Natural Language Prompt:**  
> *"Inspect the stems in `/Volumes/AudioDrives/Stems/Song_Session_A/stems` and my installed plugin library. Set up a professional Synth Rock mix for me. Import only the drum and guitar stems, instantiate Serum 2 for 'Sub Bass' and 'Synth Bass'. Construct a professional submix group setup."*

![Use Case 3: Selective Stem Ingestion, Synth Plugin Instantiation & Submix Routing](asset/usecase-3.gif)

* **Key Commands:** `daw-cli plugin scan`, `daw-cli plugin add`, `daw-cli route folder`, `daw-cli prep gain-stage`

---

## Agentic Command Examples

Here is what working in Strata Studio with an AI assistant looks like:

```text
User    ❯ Set tempo to 128 BPM, set up tracks for Kick, Snare, HH, and Tom, and balance their volume levels.

Agent   ❯ [Strata Agentic Engine]
          ✓ Set session tempo to 128.0 BPM (4/4 time signature)
          ✓ Created 4 audio tracks: Kick, Snare, HH, Tom
          ✓ Balanced volume levels across tracks 1..4 to prevent clipping
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
      <h3>🎛️ Perfect Volume & Clean Recordings</h3>
      <p>Automatically balance track levels so your song is clear, punchy, and ready for streaming—while stripping out background noise, room bleed, and dead silence from your recorded takes.</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 Instant FX & Plugin Setup</h3>
      <p>Load your favorite plugins (FabFilter, Waves, iZotope, etc.) and apply your go-to vocal or drum mixing chains across multiple tracks with a single sentence.</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎙️ Fast, Idea-First Songwriting</h3>
      <p>Build your track layout instantly, sequence beats and synth melodies, adjust volume/pan, and edit arrangement clips by talking naturally to your session assistant.</p>
    </td>
    <td width="50%" valign="top">
      <h3>⚡ Smooth, Glitch-Free Studio Performance</h3>
      <p>Play back and record huge projects with dozens of tracks and heavy plugins with crystal-clear audio—no clicks, pops, or annoying lag.</p>
    </td>
  </tr>
</table>

---

## What Your AI Agent Can Do in Strata Studio

Instead of manually clicking through complex menus, searching for shortcuts, or tweaking individual knobs, you command your AI assistant (**Claude Code**, **Cursor**, **Codex**, **Hermes**, **Gemini**, etc.) in plain English. Here is what your assistant can do in your session right now:

<table width="100%">
  <tr>
    <td width="50%" valign="top">
      <h3>🎚️ Smart Gain-Staging & Session Leveling</h3>
      <p>Ask your agent to balance track volumes, adjust panning, or auto-prep your multitracks so your mix stays clear, punchy, and headroom-ready without clipping.</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔌 VST3 / AU Plugin Hosting & FX Chains</h3>
      <p>Tell your agent to scan installed VST3/AU effect plugins (FabFilter, iZotope, Waves), insert EQs or compressors, and copy mixing chains across multiple tracks instantly.</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🎹 MIDI Sequencing & Clip Editing</h3>
      <p>Sequence drum patterns, create synth lead MIDI clips, transpose note pitch, arrange audio clips on the timeline, and slice takes using simple conversational prompts.</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔀 Bus Submixing & Auxiliary FX Routing</h3>
      <p>Group tracks into folder submix buses, configure pre/post fader auxiliary sends, and set up dynamic sidechain compression triggers across plugin chains.</p>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>⏱️ Playback Transport & Session Control</h3>
      <p>Set session tempo (BPM), adjust time signatures, control playback transport (play, stop, seek playhead), and organize track colors and names on the fly.</p>
    </td>
    <td width="50%" valign="top">
      <h3>🔬 Non-Visual DSP Analysis & Audio Intelligence</h3>
      <p>Detect Bark-scale frequency masking & collisions between tracks, detect narrow-Q resonances with musical pitch notes, compute Pearson phase correlation matrices, align multi-mic phase, and stream real-time LUFS and True-Peak telemetry.</p>
    </td>
  </tr>
</table>

### 🚀 Available AI Session Commands

* **Session State & Transport**: Set project tempo, update time signatures, jump playhead position, play/pause/stop transport.
* **Track Setup & Leveling**: Add audio/instrument tracks, set track volume & pan, mute/solo tracks, run automatic gain staging.
* **Plugin Management**: Insert VST3/AU plugins, tweak normalized parameters, copy plugin chains from one track to another.
* **Timeline & MIDI Editing**: Place audio clips, set start offsets & durations, program MIDI notes, transpose melodies.
* **Bus Submixing & Auxiliary FX Routing**: Redirect track outputs to folder group buses, configure pre/post auxiliary FX sends, link plugin sidechain compression triggers.
* **Non-Visual DSP Analysis & Audio Intelligence**: Audit Bark-scale frequency masking, detect resonant peaks, evaluate multi-track phase correlation matrices & sample-accurate offsets, stream live LUFS/dBTP telemetry, inspect 7-band spectral energy, and audit stereo width & mono compatibility.

---

## Developer Guide & Building from Source

Strata Studio is an open-source, high-performance C++20 DAW engine built for AI-driven music production. AI agents control the DAW via the [`skills/daw-cli/SKILL.md`](skills/daw-cli/SKILL.md) skill definition.

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
- [x] **Bus Submixing & Auxiliary FX Routing** (`route`) - Fully Implemented
- [x] **Non-Visual DSP Analysis & Audio Intelligence** (`analyze`) - Fully Implemented
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


