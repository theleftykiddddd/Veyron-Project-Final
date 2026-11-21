# Implementation status (v0.2.0)

| Feature | Status |
|---|---|
| Normalized macro event model | Implemented |
| Frame-indexed recorder core | Implemented |
| Geode input capture (P1/P2 press and release) | Integrated; requires in-game validation |
| Native VPR import/export | Implemented; popup uses persistent `macro-file` setting |
| GDR/GDR2, SLC, CML, tcBot, Eclipse codecs | Planned; not implemented |
| Replay event scheduler | Implemented; `drainDueEvents` returns full events |
| In-game replay injection | Integrated through `GJBaseGameLayer::handleButton`; timing validation pending |
| Practice Mode safety gate | Implemented; enabled by default |
| Replay held-input cleanup | Implemented on stop, finish, reset, and quit |
| Pause menu control popup | Implemented |
| PlayLayer HUD | Implemented; four-corner position and scale settings |
| HUD frame counter / CPS | Implemented; simulation-tick-derived approximation |
| Actual frame-window measurement engine | Pending; HUD correctly labels it unknown |
| Window bands and statistics | Implemented for supplied measured values |
| Audio chimes | Integrated using generated PCM WAV; pitch follows inter-press spacing, not measured window |
| Persistent Geode settings | Implemented in `mod.json` |
| Standalone core tests | Passed for this revision (1/1) |
| Geode Windows package | Configured in GitHub Actions; not locally compiled in this environment |
