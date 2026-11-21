# Veyron feature status

This document distinguishes the features present in v0.2.0 from ideas that remain future work.

## Delivered in v0.2.0

- Frame-indexed press/release capture for Player 1 and Player 2, with Geode settings to filter either player.
- Start and stop recording from the PauseLayer popup; autosave when recording ends, if enabled.
- Native VPR load/save using the configured Geode file setting.
- Play/pause/stop of scheduled macro events from the PauseLayer popup.
- Gameplay HUD with REC/PLAY/PAUSED status, simulation tick counter, input count, approximate one-second CPS, and explicit unknown window value.
- HUD visibility, position, and scale settings; visibility can also be toggled from the popup and is persisted.
- Short sine-wave WAV chimes with configurable volume and frequency range. The pitch is based on inter-press spacing, not a measured timing window.
- Safe Mode enabled by default; playback is blocked outside Practice Mode unless the user disables that setting.
- Cleanup of playback-held inputs on stop, pause, completion, reset, and quit.
- Platform-neutral core tests for the macro model, recorder, replay scheduler, VPR format, and analysis helpers.

## Not yet delivered

- Deterministic timing validation for every GD 2.2081 simulation path, frame-step mode, low FPS, and timewarp.
- Actual hitbox/input-window measurement and a measured window display. The HUD shows `WINDOW: ?` intentionally.
- Third-party macro formats such as GDR/GDR2, SLC, CML, tcBot, or Eclipse.
- Playback speed changes, seeking, looping, markers, and event timeline editing.
- Full run analysis reports, histograms, percentiles, CSV export, and side-by-side run comparison.
- Advanced sound profiles, stereo P1/P2 separation, and cooldown/waveform controls.
- A compiled Windows x64 Geode DLL and in-game validation against the target installation. The Geode SDK is not present in the development container used for this revision.

## Responsible use

Macro playback is intended for testing and research. It should not be submitted or represented as a human completion or proof of personal skill.
