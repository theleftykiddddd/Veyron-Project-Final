# Veyron Proyect v0.3.0 — UI and runtime polish

## Interface

- Rebuilt the pause popup as a dark compact panel with a cyan-accented title, status card, and fixed-size three-column button grid.
- Replaced text-sized Geometry Dash button sprites with consistent custom controls to prevent label-dependent overlap.
- Added live HUD ON/OFF and persistent SOUND ON/OFF labels.
- The playback control now changes between PLAY, PAUSE, and RESUME based on the actual scheduler state.
- Rebuilt the gameplay HUD as a translucent panel with a state-colored accent, compact metrics, and an explicit unmeasured-window disclaimer.
- Added configurable HUD opacity, refresh interval, and visibility switches for individual metrics.
- Corrected the popup backdrop z-order so the dark panel is drawn above the default NineSlice background.
- Moved the PauseLayer entry button away from the top-right settings gear.

## Runtime and settings

- Made the popup sound toggle persistent through Geode's `audio-enabled` setting rather than resetting on each level load.
- Added configurable HUD opacity, refresh interval, frame/input/CPS/last-action/Safe Mode visibility.
- Clamped audio pitch and volume settings at the integration boundary to protect against invalid or inconsistent ranges.
- Cached generated chime WAVs after their first creation to avoid repeated filesystem lookups during rapid input.
- Made replay-key cleanup idempotent so STOP/reset/quit does not send duplicate physical release events after a paused playback.
- Added a defensive PlayLayer hand-off that finishes any pending recording, releases held replay inputs, and clears the previous HUD before adopting a new active level.
- Kept the nominal CPS indicator explicitly approximate and retained `WINDOW UNMEASURED`.

## Core preservation and tests

- No changes to the macro data model, recorder state machine, replay scheduler, VPR serialization, input event format, or Practice Mode safety gate.
- Replay-held inputs continue to be released on stop, finish, reset, level replacement, and quit.
- Chimes continue to use Geometry Dash's FMOD backend.
- Expanded core regression checks for window analysis, input recording, stable event ordering, scheduler pause/resume/restart, and VPR round-trip.
- Replaced Release-disabled `assert` checks with explicit test checks that remain active in Release builds.

## Build organization

- Bumped project metadata to v0.3.0.
- Consolidated GitHub Actions into one workflow that runs core tests before the Windows x64 Geode build.
- Removed the duplicate workflow to avoid two parallel Geode builds on each push or pull request.

## Validation still required

- The latest branch revision must pass the Geode 5.11.2 Windows x64 CI build.
- Popup layout, HUD positioning, and readability must be checked in GD 2.2081 at different window sizes and UI scales.
- Audio, replay synchronization, and live HUD behavior must be re-tested in-game.
- The project continues to label timing windows as unmeasured; no hitbox-window measurement was added.
