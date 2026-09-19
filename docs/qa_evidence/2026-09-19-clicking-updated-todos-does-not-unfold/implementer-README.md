# #BDXG — clicking "updated tasks" unfolds to that call's task list. Implementer evidence

Implemented by Claude Opus 5 subagent (Claude Code session), 2026-09-19, in `7804df6`. QA is another
model's job: nothing in the card's checklist is ticked here.

## The live run

`drive.sh` boots the built `build/relay` under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR` (all under a short `/tmp/rly-bdxg.*` path — a unix socket has 108
bytes) and `RELAY_KEYRING=off`, and points the pane's provider at `stub-provider.py` on loopback.
That stub answers each ask with one `update_todos` tool call and then a one-line answer, and gives
the second ask a **different** list, which is what "a row from an earlier turn unfolds to that call's
list, not the current one" needs. The first list covers every glyph: completed, in_progress,
pending, deferred, blocked, cancelled.

Re-shoot with `docs/qa_evidence/2026-09-19-clicking-updated-todos-does-not-unfold/drive.sh
[build-dir]`; it needs Xvfb, xdotool, ImageMagick and tesseract. Clicks are placed by OCR-scanning
the window one terminal row at a time for the row's own text, so nothing is hard-coded to a pixel.
`implementer-notes.txt` has the OCR of every shot and the y each click landed on.

| Shot | What it shows |
| --- | --- |
| `implementer-01-row.png` | The turn ended: `▸ updated tasks · 2 open`, folded, with the ▸ the bug promised and never kept. |
| `implementer-02-unfolded.png` | **One click on that row**: the six tasks, each with its glyph — ✓ read the card (muted), ◐ make the row a fold (accent), ○ run ctest, ⏸ ask the owner, ✗ wait on the other session's header (error ink), ✕ the old plan (muted) — and the last row `open the task list`, underlined in the link ink. |
| `implementer-03-folded.png` | **A second click on the same row**: folded away again, `▸ updated tasks · 2 open`. |
| `implementer-04-two-rows.png` | A second ask leaves a second row; the first turn's fold is still open above it with its own list. |
| `implementer-05-earlier.png` | The clearest one: the **first** turn's fold open on the six-task list it left behind, the **second** turn's row folded below it, and the strip and task panel showing the *current* four-task list (T7–T10). An old row says what it said. |

`logs/` holds the app's and the worker's logs from the run and `implementer-notes.txt` the OCR of
every shot. A sixth scene — clicking `open the task list` itself — is in `drive.sh` but did not fit
the run's 900 s budget: the OCR row scan is slow, and the first five scenes are the card's claims.
Shots 02 and 05 show that row rendered and linked, and `calllines_test.cpp` pins where it points.

## What the shots cannot show, and what covers it

- **A restored pane with no call record** (the #EC58 fetch-by-anchor path) reaches the same fold
  because `handleFoldReply` fills the label from the worker's reply *before* asking for the fold's
  options, so `foldOptions` still sees `Click::Todos`. Read in `src/Pane.h`; no scene drives a
  scrollback restore.
- **A backend with no fold layer** keeps the open-call anchor (`callAnchor` gates on
  `terminalFolds()`) and shows the same `tasks` section as the call's detail in a preview pane. Only
  the libvterm core builds on this machine and it has folds, so this path is read, not shot.

## Tests

- `ctest --test-dir build` — **57 of 57 pass**, including `calllines` with the five new cases:
  the glyphed rows and their inks, the glyph table itself, an emptied list, and the last row's
  `open the task list` link over `relay://tasks/<pane>`.
- `tests/test_tool_labels.py` — the `tasks` section's source (the result's validated items), its
  fallback to the arguments when the call failed, an emptied list, and `detail()` still never raising
  on rubbish input. See `implementer-backend-tests.log`.
- `./scripts/test.sh` — 3197 tests, 3 failures, none in this card's files:
  `test_session_protocol.GuestSessionRows.test_delete_drops_the_row_and_leaves_the_guests_transcript`,
  `test_subagents.HandoffTests.test_cancelled_main_turn_does_not_wake_and_keeps_result` and
  `test_web_theme.WebThemeTest.test_committed_file_is_current` (which says in its own message that
  `app/pane-theme.css` is stale after a desktop theme change). All three belong to other sessions
  working in this shared checkout.
