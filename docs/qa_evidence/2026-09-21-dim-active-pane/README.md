# Active pane dimming — #D8AP

Added the default-off Include active pane toggle underneath Dim while working in Appearance. Stored as appearance/auto_dim_active. Automatic dimming excludes the selected pane unless opted in; opted-in busy panes remain dim after re-entry. Attention, completion and manual overrides preserve their precedence.

Validation:
- scripts/relay-build --target relay-panedimming-tests: passed.
- ctest --test-dir build -R '^panedimming$' --output-on-failure: passed. Covers default off, opt-in, parent off, re-entry, live option toggling, attention, completion and manual brightness/reveal.
- Exact proposed app tree built successfully through scripts/land.py's build gate.
- Isolated Xvfb :187, scratch workspace and XDG config/data/runtime: Options search finds the new toggle, initially unchecked (option-off.png). Clicking its row checks it (option-on.png) and persists auto_dim_active=true in the isolated relay.conf.
- No paid live agent run: busy/attention lifecycle is verified in deterministic policy tests. End-to-end live agent checks are on the card's QA checklist.
- Shared checkout app build initially failed on concurrent FilterPopup.cpp/header edits; those files were not changed here.
- Board validation reports pre-existing findings elsewhere, none on D8AP.
