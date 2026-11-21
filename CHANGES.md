# Veyron Proyect v0.3.0 — UI and runtime polish

## Interface

- Rebuilt the pause popup as a dark compact panel with a cyan-accented title, status card, and fixed-size three-column button grid.
- Replaced text-sized Geometry Dash button sprites with consistent custom controls to prevent label-dependent overlap.
- Added live HUD ON/OFF and persistent SOUND ON/OFF labels.
- Rebuilt the gameplay HUD as a translucent panel with a state-colored accent, compact metrics, and an explicit unmeasured-window disclaimer.
- Made HUD text density configurable and added panel opacity and refresh-rate settings.
- Added explicit layer anchors and corrected popup backdrop z-order so the dark panel renders above the default NineSlice background.

## Runtime and settings

- Made the popup sound toggle persist through Geode's `audio-enabled` setting rather than resetting on each level load.
- Added configurable HUD opacity, refresh interval, frame/input/CPS/last-action/Safe Mode visibility.
- Clamped audio pitch and volume settings at the integration boundary to protect against invalid or inconsistent ranges.
- Kept the nominal CPS indicator explicitly approximate and retained `WINDOW UNMEASURED`.

## Core preservation

- No changes to the macro data model, recorder state machine, replay scheduler, VPR serialization, input event format, or Practice Mode safety gate.
- Replay-held inputs continue to be released on stop, finish, reset, and level quit.
- Chimes continue to use Geometry Dash's FMOD backend.

## Build organization

- Bumped project metadata to v0.3.0.
- Consolidated GitHub Actions into one workflow that runs core tests before the Windows x64 Geode build.
- Removed the duplicate workflow to avoid two parallel Geode builds on each push or pull request.

## Validation still required

- The latest branch revision must pass the Geode 5.11.2 Windows x64 CI build.
- Popup layout, HUD positioning, and readability must be checked in GD 2.2081 at different window sizes and UI scales.
- Audio, replay synchronization, and live HUD behavior must be re-tested in-game.
- The project continues to label timing windows as unmeasured; no hitbox-window measurement was added.
