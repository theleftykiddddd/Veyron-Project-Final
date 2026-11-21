# Veyron Proyect v0.3.0 — UI polish

## Interface refresh

- Rebuilt the pause popup as a dark, compact control panel with a clear title, status card, and fixed-size three-column button grid.
- Replaced long button captions with compact labels so controls keep consistent widths and no longer overlap.
- Added dynamic HUD ON/OFF and SOUND ON/OFF labels in the popup.
- Rebuilt the gameplay HUD as a translucent panel with a state-colored accent and compact frame/input/CPS metrics.
- Kept the timing-window display explicitly unmeasured.
- Reduced the PauseLayer entry button scale slightly to make it less visually intrusive.

## Core and integration preservation

- No changes to the macro model, recorder state machine, replay scheduler, VPR serialization, event timing hooks, or Practice Mode safety gate.
- Audio continues to use Geometry Dash's FMOD backend.
- Existing lifecycle cleanup for held replay inputs remains in place.

## Build organization

- Bumped project metadata to v0.3.0.
- Consolidated GitHub Actions into one workflow that runs core tests before the Windows x64 Geode build.
- Removed the duplicate workflow to avoid two simultaneous Geode builds for each push or pull request.

## Validation still required

- The branch must pass the Geode 5.11.2 Windows x64 CI build.
- The popup and HUD layout must be checked in GD 2.2081 at the user's display resolution and UI scale.
- Audio, replay synchronization, and live HUD placement must be re-tested in-game.
- The HUD continues to label timing windows as unmeasured; no hitbox-window measurement was added.
