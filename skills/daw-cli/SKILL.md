---
name: daw-cli
description: >-
  Instructs agents to control Strata Studio DAW via the daw-cli command-line
  IPC interface. Covers session state, transport, track management, gain
  staging, VST3/AU plugin hosting, signal routing, clips, MIDI timeline editing,
  and non-visual DSP analysis & audio intelligence.
  Requires the DAW application to be running.
---

# Strata Studio DAW CLI Control

## Overview

`daw-cli` is the command-line IPC client for **Strata Studio DAW**, allowing AI agents and automation tools to query and mutate session state, manage tracks, perform gain-staging, host VST3/AU/CLAP plugins, configure signal routing & bus topology, execute clip and MIDI timeline edits, and perform non-visual DSP analysis & audio intelligence in real time.

All commands communicate with the running DAW application using a UNIX domain socket.

Binary location (if not installed on system PATH):
- Quoted path: `"./build/release/src/Agentic\ layer/daw-cli"` (Release build)
- Quoted path: `"./build/debug/src/Agentic\ layer/daw-cli"` (Debug build)
- Standard executable name (if symlinked / on PATH): `daw-cli`

## References Directory
- [error_codes.md](file:///Users/goldenfung/Documents/agent-based-daw/skills/daw-cli/references/error_codes.md): Detailed IPC error codes, trigger causes, and recovery actions.
- [command_schema.json](file:///Users/goldenfung/Documents/agent-based-daw/skills/daw-cli/references/command_schema.json): Machine-readable JSON schema defining all verbs, subcommands, flags, types, and defaults.

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

### 5. Clips & Timeline Editing (clip, midi)

#### Add Audio Clip
```bash
daw-cli clip add-audio --track 1 --path "/audio/vocal.wav" --start 1.1.0
```
- **Success Symbol**: `CLIP_ADDED`
- **Options**: `--track <uint32>`, `--path "<string>"`, `--start <BBT>`

#### Add MIDI Clip
```bash
daw-cli clip add-midi --track 1 --start 1.1.0 --dur 4.0.0
```
- **Success Symbol**: `CLIP_ADDED`
- **Options**: `--track <uint32>`, `--start <BBT>`, `--dur <BBT>`

#### Add MIDI Note Event
```bash
daw-cli midi add-note --track 1 --clip 1 --pitch C4 --velocity 100 --start 1.1.0 --dur 1.0.0
```
- **Success Symbol**: `MIDI_NOTE_ADDED`
- **Options**: `--track <uint32>`, `--clip <uint32>`, `--pitch <musical_note, e.g. C4, A5, Db5 or integer 0..127>`, `--velocity <0..127>`, `--start <BBT>`, `--dur <BBT>`

#### List Arrangement Clips
```bash
daw-cli clip list --track 1 [--bar 1-8]
```
- **Success Symbol**: `CLIP_LIST`
- **Options**: `--track <uint32>`, `--bar <start_bar-end_bar>`

#### Set Clip Gain (dB)
```bash
daw-cli clip set-gain --track 1 --clip 1 --db -3.0
```
- **Success Symbol**: `CLIP_GAIN_UPDATED`
- **Options**: `--track <uint32>`, `--clip <uint32>`, `--db <float>`

#### Mute / Unmute Clip
```bash
daw-cli clip set-mute --track 1 --clip 1 --on true
```
- **Success Symbol**: `CLIP_MUTE_UPDATED`
- **Options**: `--track <uint32>`, `--clip <uint32>`, `--on <true|false>`

#### Split Clip
```bash
daw-cli clip split --track 1 --clip 1 --at 2.1.0
```
- **Success Symbol**: `CLIP_SPLIT`
- **Options**: `--track <uint32>`, `--clip <uint32>`, `--at <BBT>`

#### Trim Silence
```bash
daw-cli clip trim-silence --track 1 --clip 1 --threshold -48.0 --fade-ms 5.0
```
- **Success Symbol**: `CLIP_SILENCE_TRIMMED`
- **Options**: `--track <uint32>`, `--clip <uint32>`, `--threshold <float_dB>`, `--fade-ms <float>`

#### Quantize Clip / MIDI
```bash
daw-cli clip quantize --track 1 --clip 1 --grid 1/16 --strength 1.0
```
- **Success Symbol**: `MIDI_CLIPS_QUANTIZED` / `CLIPS_QUANTIZED`
- **Options**: `--track <uint32>`, `--clip <uint32>`, `--grid <grid_fraction>`, `--strength <float_0..1>`

#### Merge Clips
```bash
daw-cli clip merge --track 1 --start 1.1.0 --end 5.1.0
```
- **Success Symbol**: `CLIPS_MERGED`
- **Options**: `--track <uint32>`, `--start <BBT>`, `--end <BBT>`, `--clips "<list>"`

#### Move Clip
```bash
daw-cli clip move --track 1 --clip 1 --to-pos 3.1.0 [--to-track 2]
```
- **Success Symbol**: `CLIP_MOVED`
- **Options**: `--track <uint32>`, `--clip <uint32>`, `--to-pos <BBT>`, `--to-track <uint32>`

#### Nudge Clip Position
```bash
daw-cli clip nudge --track 1 --clip 1 --by +1/16
```
- **Success Symbol**: `CLIP_NUDGED`
- **Options**: `--track <uint32>`, `--clip <uint32>`, `--by <+|-grid_fraction>`

---

### 6. Signal Routing & Topology (route)

#### Route Tracks to Bus / Folder
Routes source track outputs to a destination bus or folder track, updating both audio routing and parent folder hierarchy.
```bash
daw-cli route folder --track 1..4 --to 10
```
- **Success Symbol**: `ROUTE_FOLDER_UPDATED`
- **Options**: `--track <range>`, `--to <bus_track_id>`

#### Add Aux Send
Configures a pre-fader or post-fader send from source tracks to a target Aux track with gain level in dB.
```bash
daw-cli route send --from 1..4 --to 14 --db -12.0 --tap post
```
- **Success Symbol**: `ROUTE_SEND_ADDED`
- **Options**: `--from <range>`, `--to <aux_track_id>`, `--db <float_dB>` (Default: `-12.0`), `--tap pre|post` (Default: `post`)

#### Set Up Sidechain Connection
Links a trigger source track as a sidechain input into a target track's plugin or instrument slot.
```bash
daw-cli route sidechain --source 1 --to-track 9 --slot 0 --db 0.0
```
- **Success Symbol**: `ROUTE_SIDECHAIN_LINKED`
- **Options**: `--source <track_id>`, `--to-track <track_id>`, `--slot <0..7>` (Default: `0`), `--db <float_dB>` (Default: `0.0`)

#### List Routing Topology
Lists signal routing connections, main outputs, sends, and sidechains across all tracks.
```bash
daw-cli route list [--format tsv|kv|json|pretty]
```
- **Success Symbol**: `ROUTE_LIST`

---

### 7. Non-Visual DSP Analysis & Audio Intelligence (analyze)

Domain 6 provides a non-visual DSP analysis and audio intelligence suite across 9 specialized subcommands, supporting both **Short Frame** (real-time telemetry) and **Full Track** (offline decoded asset) processing.

#### Frequency Masking & Collision Detection
Calculates psychoacoustic masking overlap and energy density collisions across 24 critical Bark bands between two tracks using Schroeder spreading functions.
```bash
daw-cli analyze masking --track 1 --vs 2 [--format json|tsv|kv|pretty]
```
- **Success Symbol**: `ANALYSIS_MASKING`
- **Options**: `--track <uint32>`, `--vs <uint32>`
- **Output Fields**: `PRIMARY_TRACK`, `VS_TRACK`, `OVERALL_MASKING_INDEX` ($C_{A,B} \in [0.0, 1.0]$), `COLLISION_RISK` (`LOW_MASKING`, `MODERATE_MASKING`, `HIGH_MASKING`), `MASKED_BAND_COUNT`, `BAND_1_RANGE_HZ`, `BAND_1_MASK_AMOUNT_DB`, `BAND_1_RECOMMENDED_ACTION` (`SIDECHAIN_DUCK_PRIMARY_LOWS`, `CUT_VS_TRACK_EQ_<freq>HZ`), ...

#### Narrow-Q Resonance Peak Detection
Scans track audio spectrum with a 31-bin moving median spectral envelope filter to isolate harsh narrow-Q resonances, pitch note names, and Q-factor sharpness ($Q \ge 8.0$).
```bash
daw-cli analyze resonance --track 1 [--format json|tsv|kv|pretty]
```
- **Success Symbol**: `ANALYSIS_RESONANCE`
- **Options**: `--track <uint32>`
- **Output Fields**: `TRACK_ID`, `TOTAL_RESONANCES_FOUND`, `RESONANCE_1_FREQ_HZ`, `RESONANCE_1_NOTE` (e.g. `E4`, `A5`), `RESONANCE_1_PROMINENCE_DB`, `RESONANCE_1_Q_FACTOR`, `RESONANCE_1_SEVERITY` (`HIGH_SEVERITY_SURGICAL_CUT`, `MODERATE`), `RESONANCE_1_REC_NOTCH_DB`

#### Multi-Track Pearson Phase Correlation Matrix
Computes an $N \times N$ pairwise Pearson correlation matrix ($r_{i,j}$) across a list or range of tracks to flag phase cancellation risks and identify destructive anti-phase pairs.
```bash
daw-cli analyze phase-matrix --track 1..4 [--format json|tsv|kv|pretty]
```
- **Success Symbol**: `ANALYSIS_PHASE_MATRIX`
- **Options**: `--track <range|list>` (e.g. `1..4`, `1,2,3,4`)
- **Output Fields**: `TRACKS_ANALYZED`, `GLOBAL_GROUP_HEALTH` (`HEALTHY_MONO_COMPATIBLE`, `WARNING_SEVERE_CANCELLATION`), `WORST_PAIR_1`, `WORST_CORRELATION_1`, `REC_ACTION_1` (`FLIP_POLARITY_TRACK_<id>`, `NUDGE_DELAY_TRACK_<id>`), `CORRELATION_MATRIX_FLAT`

#### Cross-Correlation Phase Alignment
Calculates cross-correlation lag across $[-128, +128]$ sample offsets to recommend sample-accurate and millisecond time shifts ($\Delta t_{\text{ms}} = \frac{\text{offset}}{f_s} \times 1000$).
```bash
daw-cli analyze phase-align --track 1 --vs 2 [--format json|tsv|kv|pretty]
```
- **Success Symbol**: `ANALYSIS_PHASE_ALIGN`
- **Options**: `--track <uint32>`, `--vs <uint32>`
- **Output Fields**: `PRIMARY_TRACK`, `VS_TRACK`, `RECOMMENDED_SAMPLE_OFFSET`, `RECOMMENDED_TIME_OFFSET_MS`, `CURRENT_CORRELATION`, `IMPROVED_CORRELATION`, `RECOMMENDED_ACTION` (e.g. `NUDGE_REGION_TRACK_2_BY_-48_SAMPLES`)

#### Window Offline Telemetry
Performs offline telemetry analysis over a specified timeline range for momentary/short-term EBU R128 BS.1770 LUFS, true-peak dBTP, crest factor, and safety gain trim.
```bash
daw-cli analyze window --track 1 --start 1.1.0 --dur 4.0.0 [--format json|tsv|kv|pretty]
```
- **Success Symbol**: `ANALYSIS_WINDOW_STREAM`
- **Options**: `--track <uint32>`, `--start <BBT>`, `--dur <BBT>`
- **Output Fields**: `TRACK_ID`, `START_POS`, `DUR_POS`, `MOMENTARY_LUFS`, `SHORT_TERM_LUFS`, `SAMPLE_PEAK_DBFS`, `TRUE_PEAK_DBTP`, `IS_CLIPPING`, `CLIPPING_EVENTS_COUNT`, `CREST_FACTOR_DB`, `SPECTRAL_CENTROID_HZ`, `STEREO_CORRELATION`, `SAFETY_STATUS` (`SAFE_NORMAL`, `VIOLATION_DANGER_REDUCE_GAIN`), `REC_GAIN_TRIM_DB`

#### 7-Band Spectral Energy Balance
Evaluates acoustic energy distribution across 7 standardized bands (Sub: 20-60Hz, Bass: 60-250Hz, Low-Mid: 250-500Hz, Mid: 500-2kHz, High-Mid: 2-4kHz, Highs: 4-8kHz, Air: 8-20kHz) alongside spectral centroid, 85% energy rolloff frequency, and spectral tilt (dB/octave).
```bash
daw-cli analyze spectrum --track 1 [--format json|tsv|kv|pretty]
```
- **Success Symbol**: `ANALYSIS_SPECTRUM`
- **Options**: `--track <uint32>`
- **Output Fields**: `TRACK_ID`, `SPECTRAL_CENTROID_HZ`, `SPECTRAL_TILT_DB_OCT`, `SPECTRAL_ROLLOFF_HZ`, `SUB_BAND_DBFS`, `BASS_BAND_DBFS`, `LOW_MID_BAND_DBFS`, `MID_BAND_DBFS`, `HIGH_MID_BAND_DBFS`, `HIGHS_BAND_DBFS`, `AIR_BAND_DBFS`

#### EBU R128 Loudness Audit (Full Track / Real-Time)
Performs ITU-R BS.1770-4 / EBU R128 integrated loudness, loudness range (LRA), short-term/momentary peak LUFS, sample peak, and crest factor.
```bash
daw-cli analyze loudness --track 1 [--format json|tsv|kv|pretty]
```
- **Success Symbol**: `ANALYSIS_LOUDNESS`
- **Options**: `--track <uint32>`
- **Output Fields**: `TRACK_ID`, `INTEGRATED_LUFS`, `SHORT_TERM_MAX_LUFS`, `MOMENTARY_MAX_LUFS`, `LRA_LU`, `CREST_FACTOR_DB`, `SAMPLE_PEAK_DBFS`, `TRUE_PEAK_DBTP`

#### True-Peak Inter-Sample Clipping Audit
Performs 4x oversampled true-peak analysis to detect inter-sample clipping events and safety margin status ($0.0\text{ dBTP}$ threshold).
```bash
daw-cli analyze true-peak --track 1 [--format json|tsv|kv|pretty]
```
- **Success Symbol**: `ANALYSIS_TRUE_PEAK`
- **Options**: `--track <uint32>`
- **Output Fields**: `TRACK_ID`, `MAX_TRUE_PEAK_DBTP`, `TOTAL_CLIPPING_EVENTS`, `SAFETY_STATUS` (`SAFE_NORMAL`, `VIOLATION_DANGER`)

#### Mid/Side Stereo Width Audit
Measures Mid RMS ($0.5(L+R)$), Side RMS ($0.5(L-R)$), Mid-to-Side ratio in dB, stereo width percentage, and mono fold-down cancellation loss in dB.
```bash
daw-cli analyze stereo-width --track 1 [--format json|tsv|kv|pretty]
```
- **Success Symbol**: `ANALYSIS_STEREO_WIDTH`
- **Options**: `--track <uint32>`
- **Output Fields**: `TRACK_ID`, `MID_RMS_DBFS`, `SIDE_RMS_DBFS`, `MS_RATIO_DB`, `STEREO_WIDTH_PCT`, `MONO_FOLD_LOSS_DB`

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

### Workflow C: Submix Bus & Sidechain Setup
```bash
# 1. Route drum tracks 1..4 into Drums Bus (Track 10)
daw-cli route folder --track 1..4 --to 10

# 2. Add post-fader send from Snare (Track 2) to Reverb Aux (Track 14) at -12 dB
daw-cli route send --from 2 --to 14 --db -12.0 --tap post

# 3. Link Kick (Track 1) as sidechain trigger to Synth Bass (Track 9) slot 0
daw-cli route sidechain --source 1 --to-track 9 --slot 0 --db 0.0

# 4. Inspect session routing topology
daw-cli route list --format tsv
```

### Workflow D: Acoustic Guitar & Vocal Masking Audit and Resonant Notch Fix
```bash
# 1. Analyze frequency masking between Vocal (Track 1) and Acoustic Guitar (Track 2)
daw-cli analyze masking --track 1 --vs 2

# 2. Detect harsh resonant peaks and Q-factors on Acoustic Guitar
daw-cli analyze resonance --track 2

# 3. Evaluate multi-mic phase alignment between Top Snare (Track 3) and Bottom Snare (Track 4)
daw-cli analyze phase-align --track 3 --vs 4

# 4. Perform offline windowed LUFS & True-Peak telemetry on Master Output (Track 10)
daw-cli analyze window --track 10 --start 1.1.0 --dur 4.0.0
```

---

## Common Mistakes

1. **Plugin Name Case Sensitivity**:
   `daw-cli plugin add --name` requires an **exact case-sensitive match**. `plugin add --name "fabfilter pro-q 3"` will fail with `72 ENTITY_NOT_FOUND`. Always run `plugin list --filter "FabFilter"` first to copy the exact name string.

2. **Forgetting `--overwrite` on Occupied Plugin Slots**:
   Copying plugins (`plugin copy` or `plugin copy-chain`) to tracks with existing plugins in target slots without `--overwrite` will fail with error `74 RESOURCE_BUSY_USER_TOUCH`.

3. **Not Checking `status` Before Issuing Commands**:
   Attempting to execute commands when Strata Studio is closed returns error `71 DAW_NOT_RUNNING`. Always run `daw-cli status` first to confirm connectivity.

4. **Missing Comparison Target for Pairwise Analysis**:
   Commands like `daw-cli analyze masking` and `daw-cli analyze phase-align` require `--vs <target_track_id>`. Omitting `--vs` returns error `70 INVALID_ARGS`.
