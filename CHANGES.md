# Veyron Proyect v0.2.0 — source changes

## Game integration

- Added `src/geode/VeyronIntegration.cpp` with a `PauseLayer` control button and popup, `PlayLayer` HUD/lifecycle cleanup, and `GJBaseGameLayer` capture/replay hooks.
- Added P1/P2 press and release capture, scheduler-driven replay injection, runtime-held-input tracking, pause/resume restoration, and cleanup on stop/finish/reset/quit.
- Added Safe Mode (Practice Mode required by default), audio mute control, generated PCM sine-wave chimes, and persistent HUD/audio/capture/file settings.
- Added `drainDueEvents` to the replay scheduler so the Geode adapter receives complete events rather than only an event count.

## Validation and limitations

- Platform-neutral C++ core tests pass (1/1).
- `mod.json`, `config/defaults.json`, and `defaults.json` pass JSON syntax validation.
- The Geode adapter has not been compiled against the Geode SDK in this environment because `GEODE_SDK` is not installed here. A Windows x64 CI build and in-game validation are still required.
- The simulation tick boundary, replay synchronization, Practice Mode gate, audio playback, and popup placement must be checked in GD 2.2081.
- The HUD intentionally reports the timing window as unknown. Audio pitch follows inter-press spacing, not a hitbox/window measurement.

## Build source of truth

CMake compiles `include/veyron/`, `src/core/`, `src/geode/`, and `tests/`. The old root-level `.cpp` / `.hpp` copies are retained from the uploaded archive for compatibility/reference only; they are not compiled by the current CMake target.
