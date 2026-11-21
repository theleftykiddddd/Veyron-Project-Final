# Veyron Proyect

Geometry Dash macro recording, native VPR import/export, replay scheduling, and live input analysis.

## Target

| Component | Target |
|---|---|
| Geometry Dash | **2.2081 (Windows)** |
| Geode SDK / minimum loader version | **5.11.2** |
| Build platform | **Windows x64 (`Win64`)** |
| Project version | **v0.3.0** |

## In-game interface

The Geode integration lives in `src/geode/VeyronIntegration.cpp`.

- `PauseLayer`: adds the **VEYRON** entry point and opens the control popup.
- Control popup: fixed-size three-column controls for REC, STOP, PLAY, SAVE, LOAD, HUD, and SOUND. The popup uses a dark panel, compact status card, and short labels to prevent button overlap.
- `GJBaseGameLayer`: captures press/release inputs and processes due replay events on the simulation command hook.
- `PlayLayer`: owns the compact gameplay HUD and releases held replay inputs on stop, finish, reset, and quit.
- HUD: translucent dark panel with a state-colored accent, frame counter, input count, approximate CPS, and an explicit unmeasured-window label.
- Audio: generates short PCM sine-wave WAV chimes. Pitch follows the spacing between press inputs; it is not a measured hitbox window.
- Safe playback: enabled by default and requires Practice Mode before macro injection.
- File path: Load VPR and Save VPR use the persistent `macro-file` Geode setting. The default is `{mod_save_dir}/latest.vpr`.

## Core

The platform-neutral recorder, scheduler, VPR format, analysis bands, pitch mapping, and tests remain in `src/core` and `include/veyron`.

The replay scheduler exposes `drainDueEvents`, returning complete press/release events for the Geode adapter. The earlier `advanceTo` counter interface remains available for existing callers.

## Timing limitations

The live counter advances after each non-half `GJBaseGameLayer::processCommands` pass. Replay events are drained immediately before the corresponding vanilla command pass. This avoids treating render FPS as gameplay frames, but the timing still requires validation against GD 2.2081, frame-step mods, timewarp, and unusual tick scheduling. Playback is a testing/research feature, not a guarantee of deterministic cross-machine timing.

A macro's click spacing is not an input window. The HUD intentionally displays `WINDOW UNMEASURED` until a validated measurement backend exists. Chime pitch follows inter-press spacing only.

Third-party macro codecs (GDR/GDR2, SLC, CML, tcBot, Eclipse) are not implemented. Native `.vpr` is supported.

## Build and test

GitHub Actions runs the platform-neutral C++ tests and then builds the Geode mod for Windows x64.

- Workflow: `.github/workflows/build.yml`
- SDK: `v5.11.2`
- Target: `Win64`
- Artifact: `Veyron-Proyect-Windows-x64`

Standalone core tests can also be run locally with CMake 3.21+ and a C++23 compiler:

```bash
cmake -S . -B build -DVEYRON_STANDALONE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## In-game use

1. Open a level and pause it.
2. Select **VEYRON** to open the control panel.
3. Choose **REC**, close the popup, and resume gameplay.
4. Pause again and choose **STOP** to finish recording. Auto-save writes the macro to the configured VPR path.
5. Use **LOAD** to load a saved VPR or use a macro already recorded in memory.
6. In Practice Mode, choose **PLAY** to start playback; the same button pauses or resumes it.
7. Use **SAVE** to export the current macro, **HUD** to toggle the overlay, and **SOUND** to mute/unmute chimes.
8. Persistent position, scale, audio, and Safe Mode settings remain available through Geode's mod settings.

## Safety and records

Macro playback is intended for testing and research. Do not represent macro playback as a human completion or submit it as a personal record. Safe Mode is enabled by default and requires Practice Mode.

## Source layout

The build uses `include/veyron/`, `src/core/`, `src/geode/`, and `tests/`. Legacy root-level copies from the original archive are retained for reference and are not part of the active CMake target.
