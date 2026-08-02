# Strata Studio DAW IPC Protocol Error Code Reference

Extracted from `src/Agentic layer/common/ipc_protocol.h` and `src/Agentic layer/client/main_client.cpp`.

## Socket Details
- **Default Socket Path**: `/tmp/daw_session.sock`
- **Output Format on Error**: `ERROR <code> <SYMBOL> "<message>"`

## Error Registry

### Error 0: `OK`
- **Description**: Command executed cleanly without errors.
- **Client Action**: Proceed to next step in workflow.

### Error 70: `INVALID_ARGS`
- **Description**: Command line grammar error, missing required flag, unknown verb/subcommand, or invalid number format.
- **Common Trigger**: Omitting required flags (e.g. `--val` without argument or `--name` missing), passing non-numeric text to numeric float flags, or out-of-range slot indices (`> 7`).
- **Client Action**: Inspect command grammar and required options against `references/command_schema.json`.

### Error 71: `DAW_NOT_RUNNING`
- **Description**: IPC socket connection to `/tmp/daw_session.sock` failed. The Strata Studio DAW main process is not active or server daemon failed to bind socket.
- **Common Trigger**: Launching `daw-cli` when Strata Studio is closed.
- **Client Action**: Inform user to launch Strata Studio GUI application before proceeding.

### Error 72: `ENTITY_NOT_FOUND`
- **Description**: Target entity (Track ID, Plugin Name/ID, Clip, or Marker) does not exist in the session.
- **Common Trigger**: Using mismatched plugin name string in `plugin add --name` (requires exact case match) or target track ID out of bounds.
- **Client Action**: Execute `track list` or `plugin list` to verify exact identifiers.

### Error 73: `ENGINE_PLAYING_LOCKED`
- **Description**: Real-time audio engine transport is currently active (`PLAYING`), locking structural DAW session topology mutations.
- **Common Trigger**: Attempting graph dynamic updates or heavy track structural deletions during playback.
- **Client Action**: Call `daw-cli transport stop` first before applying session graph changes.

### Error 74: `RESOURCE_BUSY_USER_TOUCH`
- **Description**: Resource is occupied or locked by active user interaction in GUI (e.g., parameter slider drag) or target plugin slot already contains an inserted plugin.
- **Common Trigger**: Running `plugin copy` or `plugin copy-chain` to target tracks with occupied slots without passing `--overwrite`.
- **Client Action**: Pass `--overwrite` flag to replace existing plugins, or wait for user drag to finish.

### Error 75: `PLUGIN_FAULT`
- **Description**: VST3/AU/CLAP plugin host error, or parameter value outside normalized bounds `[0.0, 1.0]`.
- **Common Trigger**: Passing `--val` values `< 0.0` or `> 1.0` in `plugin set-param`.
- **Client Action**: Clamp parameter values to `[0.0, 1.0]` before calling `plugin set-param`.

### Error 76: `ASSET_I_O_ERROR`
- **Description**: Audio clip asset file unreadable, unmapped, or corrupt format on disk.
- **Common Trigger**: Referencing missing `.wav` file path on filesystem.
- **Client Action**: Verify local filesystem path validity and read permissions.
