# #SW1D implementer evidence

Built through `scripts/relay-build --target relay relay-boardpane-tests relay-profilepane-tests`.
Targeted `hygieneChecksBeforeCleanup` passed (format request only, three actions in order, stable
keys, visible findings, cleanup dry run, Stop, preview results and explicit Apply request).
`ctest --test-dir build -R '^profilepane$' --output-on-failure` passed all 11 QtTest cases.

`gui.py` drives the real application with xdotool under a fresh Xvfb and isolated XDG config,
using OCR to find buttons. Fixture staging:

```
python3 docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/stage.py --dir /tmp/claude-1000/sw1d-gui --fresh
python3 docs/qa_evidence/2026-09-21-sw1d/gui.py docs/qa_evidence/2026-09-21-sw1d
```

- `01-row.png`: exactly Hygiene (k), Tests, Performance.
- `02-hygiene.png`: real worker format-check results, Clean up as the second stage, and automatic shortcut hint.
- `03-performance.png`: existing four-target menu opened from Performance on the row.

GUI run passed. No model call is needed for Hygiene’s format check or these screenshots;
cleanup preview/apply wire behavior is covered by the widget test with supplied worker events.

The initial broader shared-tree boardpane run passed the Hygiene test but failed the parent's
uncommitted named-driver test (expected `board_move_card`). That parent test is excluded from
this commit. Board format check reports 13 existing errors and 748 warnings elsewhere; none
names SW1D. The landing build gate builds only this delivery's exact merged tree.
