# Agent-Based DAW Domain Context

This document outlines the core domain concepts, terminology, and relationships for the Agent-Based DAW project.

## Language

**Audio Clip**:
A visual and musical region on the timeline referencing a portion of an audio file.
_Avoid_: Audio Region, Sample Region

**Time Stretching**:
The pitch-preserving speed modification of an **Audio Clip**'s playback rate.
_Avoid_: Warping, Resampling, Pitch-shifting

**Loop**:
A playback range defined by a start frame and end frame where the playhead wraps.
_Avoid_: Cycle, Repeat zone

**Project BPM**:
The active tempo of the host project.
_Avoid_: Song BPM, Host BPM

**Source BPM**:
The original tempo of an audio asset, extracted from file metadata.
_Avoid_: Clip BPM, File BPM

**Track Automation Mode**:
The recording/playback state of a track's automation envelope:
- **Write-enabled (Write, Touch, Latch, Trim)**: Curves and control points are visible and fully editable on the timeline.
- **Read**: Curves and control points are visible but rendered in read-only mode (un-editable); envelope values are read during playback.
- **Off**: Curves and control points are hidden; envelope values are ignored.
_Avoid_: Focus Mode, Automation Mode, Track Mode

**Automation Clip**:
A visual and musical region residing inside an expandable sub-lane of a track, referencing a set of automation points. Automation clips can be moved, chopped, and stretched (using playbackRatio) independently of the main track timeline.
_Avoid_: Automation Item, Curve Clip, Parameter Region

## Relationships

- An **Audio Clip** has a **Source BPM** and a **Playback Ratio** determined by its **Time Stretching** settings.
- **Time Stretching** can be triggered by a user via `Shift + Drag` to alter the **Audio Clip** length without changing its pitch.
- The **Loop** determines the playhead wrapping boundaries on the timeline.

## Example dialogue

> **Dev:** "If the user drags the edge of an **Audio Clip** while holding Shift, do we perform **Time Stretching**?"
> **Domain expert:** "Yes, we stretch the audio to match the new duration while keeping the pitch constant."

## Flagged ambiguities

- "Warping" was used to describe both pitch-shifting and time-stretching—resolved: **Time Stretching** is the canonical term for pitch-preserving speed alteration.
