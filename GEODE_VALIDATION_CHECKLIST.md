# Geode in-game validation checklist

Run these checks on Windows x64 with Geometry Dash 2.2081 and Geode 5.11.2 after the CI package builds.

- [ ] The mod loads without Geode loader warnings or missing symbols.
- [ ] Opening PauseLayer displays the VEYRON button; the button opens and closes the popup without covering essential pause controls.
- [ ] REC captures P1 press/release events and P2 events when enabled.
- [ ] STOP ends recording and autosaves the configured VPR path; SAVE VPR and LOAD VPR show useful success/error feedback.
- [ ] Playback starts from tick zero of the scheduler timeline and preserves stable same-tick press/release order.
- [ ] Safe Mode blocks playback outside Practice Mode; disabling Safe Mode permits it only when the user deliberately changes the setting.
- [ ] PLAY / PAUSE releases physically held replay inputs while paused and restores the same held state on resume.
- [ ] STOP, scheduler completion, resetLevel, and onQuit release every replay-held input.
- [ ] HUD position, scale, visibility, status text, approximate CPS, and unknown window indicator render correctly at common resolutions and UI scales.
- [ ] WAV chimes play on Windows, respect volume/audio setting/runtime mute, and do not interrupt gameplay.
- [ ] Tick timing and playback are tested at normal speed, low render FPS, frame-step, timewarp, dual mode, platformer mode, and with other input mods disabled/enabled.

Do not mark deterministic playback as validated until event timestamps and input outcomes match repeatably across those conditions. The nominal metadata tick rate is 240 Hz; it is not a guarantee that every runtime path advances exactly that way.
