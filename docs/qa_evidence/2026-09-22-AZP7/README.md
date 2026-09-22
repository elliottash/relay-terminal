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
