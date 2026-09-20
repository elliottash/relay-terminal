# Signals on the Switchboard, live (#AQ6X phase 2, 2026-09-20)

What the pane does with the machine's own faults: two folded rows, a page with four actions, and a
promoted card's `## Signal` strip. Card:
`issues/features/2026-09-20-signals-a-card-type-for-machine-written-faults-s.md` (plan step 6);
design `docs/SWITCHBOARD-DESIGN.md` §4.11.2a; wire `docs/AGENT-SESSIONS-PROTOCOL.md` §32.

**Nothing here is stubbed.** The fixture writes the two files the real fold reads — the run history
(`issues/.private/tests/history.jsonl`, #7BM4) and the signals event log
(`issues/.private/signals/events.jsonl`) — through `relay_core.test_history` and
`relay_core.signals` themselves, and the running worker folds them and pushes `signals_changed`.
So every row below is what the backend half (143d4943, 7503201d) produced, not a hand-written
event:

| key | what the history says | what the fold makes of it |
|---|---|---|
| `ctest:panelayout` | failed in two consecutive runs | `broken`, **open** — decision 3 |
| `ctest:themes` | failed in two consecutive runs | `broken`, **open** |
| `ctest:voice` | the same, then dismissed by the owner (`environmental`, 7 days) | **dismissed**, behind the toggle, with its expiry |
| `ctest:queuenav` | passed in both runs | **no signal at all**, which is the point |

Re-run it with `drive.sh [relay-binary] [out-dir]`. It builds nothing: pass a binary built from the
landed tree (this run used a clean `git archive main` export built in a scratch directory, named at
the top of `ocr.txt`), because the shared `build/` holds every session's uncommitted code.

## The shots

| | |
|---|---|
| `01-board-folded.png` | The block above the first section header: `▾ 2 signals` and `▾ 1 dismissed`, both folded, with the sections under them. No signal rows, no `queuenav`. |
| `02-signals-open.png` | A click on the row: `broken ctest:panelayout ×2 · 19 min ago`, `broken ctest:themes ×2 · …`. The kind is a word — a glyph cannot say "flaky". |
| `03-dismissed-open.png` | The second toggle: `broken ctest:voice ×2 · 19 min ago · environmental · expires in 7 days`, indented under it. |
| `04-signal-page.png` | The page, in the card page's place: the key in mono, `broken · Open · ctest · ×2 · first seen 40 min ago · last seen 18 min ago`, "What failed" and the excerpt, then Claim / Release / Dismiss / Promote. Release is disabled — nobody holds it. |
| `05-dismiss-form.png` | Dismiss opens the form **in the page**: `Environmental`, a comment line, `2026-09-27` (seven days out), Cancel and Dismiss. |
| `07-claimed.png` | Claim went to the worker, which wrote it: the notice reads "Claimed ctest:panelayout". |
| `08-row-claimed.png` | Back on the list, the row now wears the chip — `⧉ board-sb closed`, because the Switchboard pane runs no agent of its own and claims under its own view token, so no pane matches it. A signal thread (phase 3) will claim under its thread id and the chip will link to it. |
| `10-card-signal-strip.png` | A promoted card's page: the `## Signal` section as a bordered strip under the pickers — "Signal — the machine's own words, rewritten on every change" — and the card's own words below it, said once. |

`ocr.txt` is the assertions, read off the shots with tesseract, plus the signals event log as the
worker left it — the dismissal the fixture wrote and the claim the GUI made:

```
{"action":"dismiss","by":"owner","comment":"this host has no sound card","key":"ctest:voice",…}
{"action":"claim","key":"ctest:panelayout","session":"board-sb…","ts":"2026-09-20T…Z","v":1}
```

`fixture.txt` is what the fixture wrote and what `signals.state()` folded out of it before the GUI
ever saw it, so the two can be compared.

The shots were taken again after `72d830f9`, which moved the pane's one `signals_list` from the
`board` handler to "when the pane is on screen": the rows still arrive, which is what that commit
had to be proved not to break.

## Read on the shots

Two assertions are about type tesseract does not read at this size, and the shots hold both: the
comment field's placeholder in `05` (the assertion checks Cancel instead), and the count in
`▸ 2 signals`, which OCR runs together as `2signals` (matched as `2 *signals`).

## Not in this run

- **A promotion from the GUI.** Promote is on the page and sends `signals_promote`; the worker's
  `promote_signal` writes the card. The run above claims instead, because the claim is the one
  write whose result is visible on the row itself.
- **A signal thread claiming.** That is phase 3 (decision 9): the thread, its notification and the
  Sessions row. Today's chip reads `closed` for the reason given above.
