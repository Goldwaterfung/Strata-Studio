---
name: daw-cli
description: >-
  Instructs agents to control Strata Studio DAW via the daw-cli command-line
  IPC interface. Covers session state, transport, track management, gain
  staging, and VST3/AU plugin hosting. Requires the DAW application to be
  running.
---

# Strata Studio DAW CLI Control

## Overview

`daw-cli` is the command-line IPC client for **Strata Studio DAW**, allowing AI agents and automation tools to query and mutate session state, manage tracks, perform gain-staging, and host VST3/AU/CLAP plugins in real time.

All commands communicate with the running DAW application using a UNIX domain socket.

Binary location (if not installed on system PATH):
- Quoted path: `"./build/release/src/Agentic\ layer/daw-cli"` (Release build)
- Quoted path: `"./build/debug/src/Agentic\ layer/daw-cli"` (Debug build)
- Standard executable name (if symlinked / on PATH): `daw-cli`

## References Directory
- [error_codes.md](file:///Users/goldenfung/Documents/agent-based-daw/skills/daw-cli/references/error_codes.md): Detailed IPC error codes, trigger causes, and recovery actions.
- [command_schema.json](file:///Users/goldenfung/Documents/agent-based-daw/skills/daw-cli/references/command_schema.json): Machine-readable JSON schema defining all verbs, subcommands, flags, types, and defaults.

---

## Agent Integration & Skill Setup

`daw-cli` can be loaded and controlled by AI coding assistants and autonomous agents (such as **Codex**, **Claude Code**, **Claude Co-Work**, **Hermes**, **Antigravity**, **Gemini CLI**, **OpenCode**, etc.).

### Installation & Discovery Paths

To enable an agent to discover and use this skill, place or symlink the `skills/daw-cli/` directory into the expected skill path for your agent framework:

| Agent Framework | Workspace Local Skill Path | Global User Skill Path |
| :--- | :--- | :--- |
| **Codex** | `.agents/skills/daw-cli` | `~/.agents/skills/daw-cli` |
| **Claude Code / Co-Work** | `.claude/skills/daw-cli` | `~/.claude/skills/daw-cli` |
| **Hermes** | `.hermes/skills/daw-cli` | `~/.hermes/skills/daw-cli` |
| **Antigravity** | `.agents/skills/daw-cli` | `~/.gemini/config/skills/daw-cli` |
| **Gemini CLI** | `.gemini/skills/daw-cli` | `~/.gemini/skills/daw-cli` |
| **OpenCode** | `.opencode/skills/daw-cli` | `~/.config/opencode/skills/daw-cli` |

### How Agents Control the DAW

1. **Natural Language Translation**: When a user prompts the agent (e.g., *"Set tempo to 128 BPM and gain-stage drum tracks 1 to 4 to -18dB"*), the agent references this `SKILL.md` to map intent into precise `daw-cli` execution strings.
2. **CLI Command Invocation**: Agents issue commands via standard terminal subshell or execution tool (using `daw-cli` or the quoted build path):
   ```bash
   # If daw-cli is on PATH:
   daw-cli transport set-tempo --bpm 128.0
   daw-cli prep gain-stage --track 1..4 --target-rms -18.0

   # Or using explicit build binary path:
   "./build/release/src/Agentic\ layer/daw-cli" transport set-tempo --bpm 128.0
   ```
3. **Structured Response Handling**: Agents should append `--format json` for programmatically structured JSON responses and verify exit code `0` before proceeding to subsequent operations.

---

## Prerequisites

1. **DAW Application Must Be Running**: The GUI/Server application must be active before running `daw-cli`.
2. **UNIX Socket Path**: The server listens on `/tmp/daw_session.sock`.
3. **Connectivity Check**: Always verify connection state first before executing batch commands:
   ```bash
   daw-cli status
   ```
   If the DAW is not running, `daw-cli` will fail immediately with exit code `71` (`DAW_NOT_RUNNING`).

---

## Output Formats & Parsing

By default, queries return `TSV` (Tab-Separated Values) for list commands and `KV` (Key-Value) for single-entity or status commands.

Agents can explicitly request machine-parsable formats using the `--format` flag:

```bash
daw-cli track list --format json
```

Supported formats:
- `--format json`: Compact JSON string (Recommended for programmatic parsing).
- `--format tsv`: Tab-Separated Values with header line (Default for lists).
- `--format kv`: `Key: Value` line pairs (Default for inspect/status).
- `--format pretty`: Human-readable structured block output.

---

## Error Code Reference

When a command fails, `daw-cli` writes to `stderr` formatted as: `ERROR <code> <SYMBOL> "<message>"` and exits with the corresponding code.

| Exit Code | Symbol | Cause | Recommended Agent Recovery |
| :--- | :--- | :--- | :--- |
| `0` | `OK` | Command completed cleanly | Proceed to next step |
| `70` | `INVALID_ARGS` | Missing required flag, bad value format, or unknown subcommand | Check command syntax, required flags, and numeric value types |
| `71` | `DAW_NOT_RUNNING` | Cannot connect to `/tmp/daw_session.sock` | Prompt user to start Strata Studio DAW |
| `72` | `ENTITY_NOT_FOUND` | Track ID, plugin name, clip, or slot index missing | Run `track list` or `plugin list` to verify exact identifiers |
| `73` | `ENGINE_PLAYING_LOCKED` | Attempted structural edit restricted during playback | Stop transport (`daw-cli transport stop`) before mutating topology |
| `74` | `RESOURCE_BUSY_USER_TOUCH` | Destination slot is occupied or user is dragging control | Add `--overwrite` flag for plugin slots or retry after user edit |
| `75` | `PLUGIN_FAULT` | VST3/AU parameter out of bounds (`0.0`..`1.0`) or host crash | Verify normalized parameter ranges |
| `76` | `ASSET_I_O_ERROR` | Audio clip path unreadable or invalid file format | Check file path existence and audio file permissions |

---

## Track Range Syntax

Commands accepting track parameters (`--track`, `--tracks`, `--to-tracks`) support integer ranges and comma-separated lists:

- Single track: `--track 1`
- Contiguous range: `--track 1..4` (expands to tracks 1, 2, 3, 4)
- Multi-range / list: `--track 1..4,7..10` (expands to 1, 2, 3, 4, 7, 8, 9, 10)

---

## Command Reference

### 1. Session State & Transport

#### Status Query
Returns current playback status, tempo, time signature, and playhead position.
```bash
daw-cli status
```
- **Success Symbol**: `STATUS_OK`
- **Output Fields**: `TRANSPORT_STATE`, `TEMPO`, `TIME_SIGNATURE`, `POSITION_BARS`, `POSITION_SECONDS`

#### Start Playback
```bash
daw-cli transport play
```
- **Success Symbol**: `PLAYBACK_STARTED`

#### Stop Playback
```bash
daw-cli transport stop
```
- **Success Symbol**: `PLAYBACK_STOPPED`

#### Set Tempo
```bash
daw-cli transport set-tempo --bpm 128.0
```
- **Success Symbol**: `TEMPO_UPDATED`
- **Options**: `--bpm <float>` (or `--tempo <float>`)

#### Set Time Signature
```bash
daw-cli transport set-time-signature --num 4 --den 4
```
- **Success Symbol**: `TIME_SIGNATURE_UPDATED`
- **Options**: `--num <int>`, `--den <int>`

---

### 2. Tracks & Gain-Staging

#### Create Track
```bash
daw-cli track create --name "Vocal Lead" --type audio --color yellow
```
- **Success Symbol**: `TRACK_CREATED`
- **Options**:
  - `--name "<string>"` (Default: "Untitled Track")
  - `--type audio|midi|instrument|aux|folder` (Default: "audio")
  - `--color <red|blue|green|yellow|purple|orange|cyan|pink|white|grey|black|#HEX>`

#### Create Tracks in Batch
```bash
daw-cli track create-batch --names "Kick,Snare,HH,Tom" --type audio
```
- **Success Symbol**: `BATCH_TRACKS_CREATED`
- **Options**: `--names "<comma_separated_list>"`, `--type audio|midi|instrument|aux|folder`

#### List All Tracks
```bash
daw-cli track list [--format json]
```
- **Success Symbol**: `TRACK_LIST`

#### Inspect Track Details
```bash
daw-cli track inspect --track 1
```
- **Success Symbol**: `TRACK_INSPECT`

#### Set Track Gain (dB)
```bash
daw-cli track set-gain --track 1..4 --db -3.5
```
- **Success Symbol**: `TRACK_GAIN_UPDATED`
- **Options**: `--track <range>`, `--db <float_in_dB>`

#### Set Track Pan Position
```bash
daw-cli track set-pan --track 1 --value -0.25
```
- **Success Symbol**: `TRACK_PAN_UPDATED`
- **Options**: `--track <range>`, `--value <float>` (`-1.0` Left to `+1.0` Right)

#### Mute / Unmute Track
```bash
daw-cli track set-mute --track 2 --on
```
- **Success Symbol**: `TRACK_MUTE_UPDATED`
- **Options**: `--track <range>`, `--on` (omit `--on` to unmute)

#### Solo / Unsolo Track
```bash
daw-cli track set-solo --track 2 --on
```
- **Success Symbol**: `TRACK_SOLO_UPDATED`
- **Options**: `--track <range>`, `--on` (omit `--on` to unsolo)

#### Delete Tracks
```bash
daw-cli track delete --track 4
```
- **Success Symbol**: `TRACK_DELETED`
- **Options**: `--track <range>`

#### Set Track Color
```bash
daw-cli track set-color --track 1..4 --color red
```
- **Success Symbol**: `TRACK_COLOR_UPDATED`

#### Auto-Color Tracks
Assigns semantic color palette based on instrument track names (e.g., Drums -> Red, Bass -> Purple, Vocals -> Yellow).
```bash
daw-cli track auto-color
```
- **Success Symbol**: `TRACKS_AUTO_COLORED`

#### Sanitize Track Names
Trims whitespace, removes extensions (`.wav`, `.mp3`), and converts underscores to spaces across all tracks.
```bash
daw-cli track sanitize-names
```
- **Success Symbol**: `TRACK_NAMES_SANITIZED`

---

### 3. Plugin Management

#### Scan Plugin Library
Triggers host scanner for installed VST3/AU plugins.
```bash
daw-cli plugin scan
```
- **Success Symbol**: `PLUGIN_SCAN_COMPLETED`

#### List Discovered Plugins
```bash
daw-cli plugin list --filter "FabFilter" --category effect --head 10 --format json
```
- **Success Symbol**: `PLUGIN_LIST`
- **Options**:
  - `--filter "<substring>"`: Case-insensitive search on name or vendor.
  - `--category effect|instrument` (or `fx`, `synth`).
  - `--head <int>` / `--tail <int>`: Limit output row count.

#### Add Plugin to Track Slot
```bash
daw-cli plugin add --track 1 --name "FabFilter Pro-Q 3" --slot 0
```
- **Success Symbol**: `PLUGIN_ADDED`
- **Options**:
  - `--track <range>`
  - `--name "<exact_name>"` (Must match exact case returned by `plugin list`) or `--id <uint32>`
  - `--slot <0..7>` (Default: 0)

#### Set Plugin Parameter Value
```bash
daw-cli plugin set-param --track 1 --plugin 0 --param 3 --val 0.75
```
- **Success Symbol**: `PLUGIN_PARAM_UPDATED`
- **Options**:
  - `--track <uint32>`
  - `--plugin <slot_0..7>` (or `--slot`)
  - `--param <param_index>`
  - `--val <float_0.0_to_1.0>` (Normalized range)

#### Copy Single Plugin to Other Tracks
```bash
daw-cli plugin copy --from-track 1 --slot 0 --to-tracks 2..5 --overwrite
```
- **Success Symbol**: `PLUGIN_COPIED`
- **Options**:
  - `--from-track <uint32>`
  - `--slot <0..7>`
  - `--to-tracks <range>`
  - `--overwrite`: Required if target slots are already occupied.

#### Copy Entire Plugin Insert Chain
```bash
daw-cli plugin copy-chain --from-track 1 --to-tracks 2..5 --overwrite
```
- **Success Symbol**: `PLUGIN_CHAIN_COPIED`
- **Options**:
  - `--from-track <uint32>`
  - `--to-tracks <range>`
  - `--overwrite`: Required if any target slot in 0..7 is occupied.

---

### 4. Session Preparation (prep)

#### Gain-Stage Track Headroom
Adjusts track fader gains to achieve requested target RMS headroom.
```bash
daw-cli prep gain-stage --track 1..8 --target-rms -18.0
```
- **Success Symbol**: `GAIN_STAGE_COMPLETED`
- **Options**: `--track <range>`, `--target-rms <float_dB>` (Default: `-18.0`)

---

## Common Workflows

### Workflow A: Setting Up a 4-Track Drum Session
```bash
# 1. Verify DAW application connection
daw-cli status

# 2. Batch-create drum tracks
daw-cli track create-batch --names "Kick,Snare,HiHat,Tom" --type audio

# 3. Clean up track names and apply instrument colors
daw-cli track sanitize-names
daw-cli track auto-color

# 4. Gain stage all drum tracks to -18 dB RMS headroom
daw-cli prep gain-stage --track 1..4 --target-rms -18.0
```

### Workflow B: Applying and Distributing an EQ Plugin Chain
```bash
# 1. Search for available EQ plugins
daw-cli plugin list --filter "EQ" --category effect --format json

# 2. Add EQ plugin to slot 0 of Vocal Lead (Track 1)
daw-cli plugin add --track 1 --name "FabFilter Pro-Q 3" --slot 0

# 3. Copy EQ plugin from Track 1 slot 0 to Backing Vocals (Tracks 2..5)
daw-cli plugin copy --from-track 1 --slot 0 --to-tracks 2..5 --overwrite
```

---

## Common Mistakes

1. **Plugin Name Case Sensitivity**:
   `daw-cli plugin add --name` requires an **exact case-sensitive match**. `plugin add --name "fabfilter pro-q 3"` will fail with `72 ENTITY_NOT_FOUND`. Always run `plugin list --filter "FabFilter"` first to copy the exact name string.

2. **Forgetting `--overwrite` on Occupied Plugin Slots**:
   Copying plugins (`plugin copy` or `plugin copy-chain`) to tracks with existing plugins in target slots without `--overwrite` will fail with error `74 RESOURCE_BUSY_USER_TOUCH`.

3. **Not Checking `status` Before Issuing Commands**:
   Attempting to execute commands when Strata Studio is closed returns error `71 DAW_NOT_RUNNING`. Always run `daw-cli status` first to confirm connectivity.
