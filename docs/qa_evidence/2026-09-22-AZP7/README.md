# AZP7 implementer evidence

Root cause: RelayWindow consumed terminal.zoom shortcuts globally but dispatched them only to m_active (the terminal). Auxiliary read-only Qt transcripts already handled Ctrl+wheel themselves.

Keyboard dispatch now targets the active auxiliary read-only transcript; Ask/header focus falls back to its visible transcript. Unsupported auxiliary panes do not accidentally zoom the owner terminal. Ctrl+0 restores the font remembered before the first keyboard or wheel zoom. Wheel use teaches the live shortcut. Explicit terminal targets still use the terminal backend.

Validation:
- `scripts/relay-build --reconfigure --target relay-auxiliaryzoom-tests relay`: passed (application build 2026-09-22.19H.02). A first attempt without reconfigure could not find the newly added test target; reconfiguration resolved it.
- `scripts/relay-build --reconfigure --target relay-auxiliaryzoom-tests`: passed after adding the actual Activity fixture.
- `xvfb-run -a build/relay-auxiliaryzoom-tests`: 6 passed, 0 failed; tests.txt records output.
- Activity fixture uses its real AgentInternalsView with deterministic synthetic reasoning. Screenshots before, +4 points, and reset exercise the same zoom helper invoked by keyboard dispatch. Zoomed screenshot visually inspected. This is widget staging, not an end-to-end keyboard event recording or sphinxpad verification.
- Tests cover Activity, Ask focus, plain/rich text, reset after Qt zoom, editable/hidden widgets, and another pane isolation.
- Board check: 505 cards, 12 pre-existing errors and 754 warnings; no AZP7 findings. No full suites run.

Files: src/AuxiliaryZoom.h, src/RelayWindow.h (three narrow hunks), tests/auxiliaryzoom_test.cpp, CMakeLists.txt, the AZP7 card/thread, and this evidence directory. Other sessions' changes excluded at landing.

Landed implementation: `7c0774c8fa8cbcf3121ac5b0feb738c24c7e804c`. The land.py exact-tree application build passed. A fresh `tests_check` through TestsCommands after landing reports auxiliaryzoom passed for revision 7c0774c8, no findings/signals. Activity staging was also rerun under Xvfb with an isolated temporary XDG_CONFIG_HOME: 3 passed, 0 failed (activityView plus init/cleanup).


## Full-window keyboard dispatch follow-up

`live-drive.py` launches the exact-tree full Relay binary under Xvfb with isolated XDG directories and a synthetic stdio worker (no provider calls in the recorded successful run). It opens Activity with Alt+Shift+R, supplies deterministic reasoning through worker events, focuses Activity with Alt+Right, and sends actual X11 Ctrl+= three times, Ctrl+minus once, and Ctrl+0. It then repeats zoom/reset after Tab traversal. The earlier widget fixture did not test window dispatch; this drive does.

`live-before.png`, `live-plus.png`, `live-minus.png`, and `live-reset.png` show that sequence. OCR measures the word “Checking” as 66, 85, 80, then 66 pixels wide. Activity's content region is pixel-identical to baseline after reset. The owner terminal's content region is pixel-identical across all six captures. `live-results.json` records measurements, input sequence and binary hash; assertions passed. Before/plus images were visually inspected.

`live-ask-plus.png` and `live-ask-reset.png` name the additional Tab-traversal sequence. Activity has Ask chips, not an editable Ask field; focus on a particular chip was not independently measured, so these are not claimed as proof of Ask-input focus. The synthetic editable-field fallback remains covered by the Qt test.

An initial exploratory run used an old provider-setting recipe and selected the default provider; it was stopped. Another fixture with no presets opened Models. Those captures were replaced by the successful deterministic-worker run above. No production code changed for this follow-up. Sphinxpad verification remains separate.

Reproduce from any directory with `python3 /path/to/relay-terminal/docs/qa_evidence/2026-09-22-AZP7/live-drive.py`. The driver defaults to that checkout's `build/relay`; set `RELAY_TEST_BINARY=/absolute/path/to/relay` to test another build. Results record the resolved binary path and SHA-256. The main agent independently reran the full driver and confirmed before/plus/reset and owner-terminal isolation.
