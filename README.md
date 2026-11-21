# Veyron Proyect

Geometry Dash macro recording, VPR import/export, replay scheduling, and live input analysis.

## Target

| Component | Target |
|---|---|
| Geometry Dash | **2.2081 (Windows)** |
| Geode SDK / minimum loader version | **5.11.2** |
| Build platform | **Windows x64 (`Win64`)** |
| Project version | **v0.2.0** |

## Current implementation

The repository keeps a platform-neutral core and connects it to Geode from `src/geode/VeyronIntegration.cpp`.

- `PauseLayer`: adds a Veyron button and opens the control popup.
- Control popup: start/stop recording, play/pause/stop replay, load/save VPR, toggle HUD, and mute chimes.
- `GJBaseGameLayer`: captures press/release input events and advances playback events from the simulation command hook.
- `PlayLayer`: owns the live HUD and cleans up held playback inputs on reset/quit.
- HUD: displays REC/PLAY/PAUSED state, simulation tick counter, recorded input count, approximate CPS, and the current status.
- Audio: creates short PCM sine-wave WAV chimes in the mod save directory. Pitch maps the interval between consecutive press inputs; this is not a measured hitbox window.
- Safe playback: enabled by default and blocks macro injection unless Practice Mode is active.
- File path: Load VPR and Save VPR use the same persistent `macro-file` Geode setting. The initial path is `{mod_save_dir}/latest.vpr`.

The existing recorder, scheduler, VPR format, analysis bands, pitch mapping, and tests remain in `src/core` and `include/veyron`. The scheduler now also exposes `drainDueEvents`, which returns the actual press/release events required by the Geode adapter. The old `advanceTo` counter interface remains available for existing callers.

## Important timing limitations

The live counter advances after each non-half `GJBaseGameLayer::processCommands` pass. Replay events are drained at the current counter value immediately before the vanilla command pass, so capture and injection share the same frame boundary. This avoids treating render FPS as gameplay frames, but it still needs validation against the exact 2.2081 runtime, frame-step mods, timewarp, and unusual tick scheduling. Playback is therefore a practical test/research feature, not a claim of deterministic cross-machine timing.

A macro's click spacing is not an input window. The HUD intentionally displays `WINDOW: ?` until a validated measurement backend exists. Chime pitch uses inter-press spacing only and must not be interpreted as a measured timing window.

Third-party macro codecs (GDR/GDR2, SLC, CML, tcBot, Eclipse) are still not implemented. Native `.vpr` is supported.

## Build and test

### Standalone core tests

Requires CMake 3.21+ and a C++23 compiler:

```bash
cmake -S . -B build -DVEYRON_STANDALONE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Geode mod

The SDK is not bundled in this repository. Install/configure Geode SDK 5.11.2 for Win64 and set `GEODE_SDK` to its source directory, then build using the Geode CMake workflow. The GitHub Actions workflow uses `geode-sdk/build-geode-mod` with `sdk: v5.11.2` and `target: Win64`.

The standalone core test suite was run successfully for this revision. The Geode integration has not been compiled against a local SDK in this environment, so a Windows CI build and an in-game test are still required before treating the package as release-ready.

## In-game use

1. Open a level. Use the pause menu's **VEYRON** button.
2. Choose **REC**, close the popup, and resume gameplay.
3. Pause again and choose **STOP** to finish recording. With Auto-save enabled, the macro is written to the configured VPR path.
4. Use **LOAD VPR** to load that file, or use a recorded macro already in memory.
5. In Practice Mode, choose **PLAY / PAUSE**, then resume gameplay. The stop control releases any inputs still held by playback.
6. Use **HUD** to toggle the overlay for the current level. For persistent position/scale/audio/safety options, use Geode's mod settings.

## Safety and records

Macro playback is intended for testing/research. It should not be represented as a human completion or submitted as a personal record. Safe Mode is enabled by default and requires Practice Mode for playback.

## Source layout note

The build uses `include/veyron/`, `src/core/`, `src/geode/`, and `tests/`. Root-level `.cpp` / `.hpp` files are retained as legacy copies from the uploaded archive for reference; they are not part of the current CMake target. See `CHANGES.md` for the complete revision summary.
