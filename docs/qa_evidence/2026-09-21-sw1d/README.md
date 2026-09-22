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

Final landing: `e325845d59af145ea6b692d38e7c526ad48b9f2e` passed the exact-tree app build.
The exported tree was also built through its `scripts/relay-build` for both test targets:

```
ctest --test-dir /tmp/claude-1000/land/sw1d/verify/src/build -R '^(boardpane|profilepane)$' --output-on-failure
```

Both complete suites pass (2/2, 0.25 seconds), including all existing card navigation checks.
The parent named-driver test is absent from this isolated delivery tree. The recorded
profilepane run `20260922T013125Z-cf42` passed without opening a signal; subsequent
`TestsCommands.check_card("SW1D")` returned no findings.

Follow-up: registered the `profile` pane type as **Performance**, including its outer chrome
band (`04-performance-pane.png`, real isolated Xvfb application, first menu target opened).
Existing board-model cleanup scenarios now drive Hygiene, receive the format result, then
exercise stage two. All 87 board-model cases pass. Combined `board`, `boardpane`, `profilepane`
and `panestatus` CTest run passes 4/4 (2.69 seconds); recorded run
`20260922T013718Z-2031` also passes all four with no signals opened. `tests_check` has only the
historical board-suite speed advisory (p95 2.58 s versus p50 0.93 s), no missing/failing test.
The parent verification-brief assertions were present in the snapshot and excluded from this
follow-up's own hunks.
