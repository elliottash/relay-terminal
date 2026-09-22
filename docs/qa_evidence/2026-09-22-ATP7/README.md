# #ATP7 implementation evidence

The old top-left QLabel used a 22-logical-pixel app tile beside 14-pixel control glyphs, without a fixed widget box. It also cached a pixmap using the application-wide DPR at construction. The replacement `ChromeAppIcon` keeps the same app artwork in an 18×18 logical-pixel box and paints via QIcon at render time so Qt uses the current paint device scale. No global icon or theme assets changed.

- `scripts/relay-build --target relay`: PASS, build 2026-09-22.19H.01. Existing unrelated `Pane::ForkText::guest` initializer warning.
- `python3 docs/qa_evidence/2026-09-22-ATP7/stage.py`: PASS against the actual Relay executable under Xvfb with isolated XDG configuration at `QT_SCALE_FACTOR=1` and `2`.
- `geometry.json`: both scales report icon 18×18, neighboring bell 26×26, corner 40×24; icon bounds lie inside its corner.
- `scale-1.png` and `scale-2.png`: full application captures inspected; the top-left app tile is smaller than controls and does not clip or grow the row. The script dismisses the optional instructions dialog before capture.
- `python3 scripts/relay-board.py check`: existing board-wide failures (12 errors, 754 warnings at the initial check), no ATP7 diagnostic. No unrelated board repairs made.

This is local staged evidence, not a sphinxpad compositor/DPI reproduction. Physical screenshots scale with DPR while logical widget bounds remain identical. Moving a live window between physical displays has not been tested. Board/delegation MCP methods and `tests_check` are not exposed in this guest; policy file fallback was used.
