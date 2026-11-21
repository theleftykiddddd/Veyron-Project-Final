# Geode integration notes

## Files

- `src/geode/main.cpp`: load log and analysis-band registration.
- `src/geode/VeyronIntegration.cpp`: PauseLayer popup, PlayLayer HUD/lifecycle, GJBaseGameLayer input capture and replay injection, file controls, settings access, and WAV chimes.
- `include/veyron/Replay.hpp` / `src/core/Replay.cpp`: `drainDueEvents(frame)` gives the Geode adapter the full event payloads while retaining `advanceTo(frame)` for compatibility.
- `mod.json`: persistent HUD, audio, capture, safety, autosave, and macro-path settings.

## Input and replay flow

1. The `GJBaseGameLayer::handleButton` hook captures real input transitions when the active layer is the current PlayLayer.
2. Each recorded event contains a relative simulation frame, button number, player index, and press/release state.
3. Before the vanilla non-half `processCommands` pass, the adapter drains events due at the current simulation tick; after the vanilla pass it advances the tick counter. This keeps captured and injected events on the same frame boundary.
4. Injected transitions call `handleButton` with a guard set so synthetic events are not recorded again.
5. Replay tracks held inputs. Pause temporarily releases physical inputs and restores them on resume; stop/reset/quit clears them. On completion, final held-input cleanup is deferred until after that tick’s vanilla simulation pass.

## Validation still required

- Build the Geode target with SDK v5.11.2 on Windows x64.
- Verify exact `processCommands` tick semantics in GD 2.2081 at normal speed, low FPS, frame-step, and timewarp settings.
- Verify replay input order and button/player mapping in cube, ship, wave, dual, and platformer modes.
- Verify `SimpleAudioEngine` WAV playback in the target Windows runtime.
- Verify the practice-mode field and PauseLayer placement in the exact target build.

The integration deliberately does not label inter-press spacing as a hitbox timing window. A measurement backend needs controlled, validated timing-variation trials before the HUD can display real window data.
