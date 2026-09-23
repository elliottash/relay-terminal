# Sessions recap table and explicit preview — 2026-09-23

`list.png` and `preview.png` are captures of the actual `SessionManager` widget under Xvfb,
using two representative conversations. The table opens at full width, displays a saved recap
or “No recap saved”, and stays visible after a single row click. Preview (P) replaces the table
with the selected conversation. Back, Escape, a double click, P, Enter and Resume were exercised
by the widget test. The recap header sends ascending and descending sort requests to the worker.

Checks:

- `scripts/relay-build --target relay`: passed (full app).
- `scripts/relay-build --target relay-conversations-tests`: passed.
- `RELAY_SHOT_DIR="$PWD/docs/qa_evidence/2026-09-23-sessions-recap-preview" xvfb-run -a build/relay-conversations-tests sessionsTableShowsRecapAndPreviewsOnDemand headerClickSortsByThatColumn rowsCarryTheirSummaryAndTags unfoldAsksOnceAndFillsFromTheOverview summariesFromTheButtonAndTheBatch groupRowsSpanTheWidth`: 8 passed, 0 failed.
- `PYTHONPATH=backend python3 -m unittest tests.test_conv_index`: 122 passed.
- Full `conversations` CTest run: 49 passed, 1 failed, 2 skipped. The failure is the earlier mouse popup audit's `chooseComboItem(group, "none")` assertion when run after the preceding widget tests; the same audit passes in isolation. The root of that order-dependent test failure remains open.

Recap generation on pane close is a separate design choice in #RCP9. The column currently reads
the existing persisted `summary`; Relay's automatic summary cadence may leave it older than the
last turn of a recently closed session.
