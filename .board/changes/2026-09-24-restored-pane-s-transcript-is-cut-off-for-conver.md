---
id: KDB4
type: work
status: needs-verification
labels: [bug, restore, sessions]
assignee: agent
implemented_by: glm/glm-5.3
session: 0d5e4e19-1779-4d30-bad8-4ba53ba154e0
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: text, primary: script, also: [probe], human: none, criteria: 'unit tests pass; coveredFrom(5fa1 saved lines, 5fa1 items) = 8', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: pane 1, 2026-09-24
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-restored-transcript-fill/], related: [], github: null}
---
# Restored pane's transcript is cut off for conversations longer than the saved-text window

## Issue
bug. on restore, sometimes the transcript is getting cut off, see here for example:

69c2c9d3

A pane restored after a restart shows its conversation's transcript cut off. Example: pane `69c2c9d3` ("Verify image generation implementation", session `5fa1d5a6…`), restored 2026-09-24 21:47:54Z.

Measured evidence:

- The conversation has 8 turns / 176 entries in the index, spanning 13:01→17:44.
- Its saved terminal text (`sessions/83e6ce670835f99d/5fa1d5a6….scrollback.txt`, 430 lines) starts mid-word — `the implementer. Next is the desktop again / st a loc / al gateway` — the tail of turn 7's last message. **Turns 1–6 are gone from the restored transcript.**

Cause chain (all in current `main`):

1. `sessiontext` is capped at `kScrollbackMaxLines` = 5,000 lines / 512 KiB, newest kept (`clampScrollback`), and the engine ring feeding it holds 10,000 lines (`LibVtermCore` default `limit`, the GUI never raises it). A 4h48m codex conversation prints far more than either; the save keeps only the newest window. The code comment in `Pane::sessionTextLines()` calls this "the approximation the plan allows for" (docs/ARCHITECTURE.md 14.4 says the same).
2. On restore, `queueSessionTextReplay(path)` succeeds whenever the file exists, and the full-transcript fallback `requestSavedTranscript()` only runs when it does not. So a truncated file permanently hides the older turns — even though the worker's index holds every turn and `printSavedTranscript()` can render them.
3. The truncation compounds: every restore replays the cut file and later re-saves from it.

Sibling defect found on the way: `conv_index.conversation()` returns `ORDER BY turn, seq LIMIT n` — the **oldest** n entries — so the no-file fallback itself is head-complete but tail-missing for conversations with more than `limit` entries (clamped to 2,000). Both ends of the same symptom.

## Done means

- A restored pane whose saved terminal text no longer covers the conversation's earlier turns shows those turns anyway — drawn from the worker's transcript — above the saved text, between rules that name what they are.
- The saved text's own window (5,000 lines, newest kept) is unchanged; the fill is a rendering, not a rewrite of the file.
- When the saved text does cover the whole conversation (or nothing matches), the restore looks exactly as it does today.
- `conversation_get` returns the newest entries, not the oldest, when the limit bites.
- Unit tests cover the turn matching, the fill rendering, and the newest-kept ordering.

## Plan

1. `src/TranscriptReplay.h`: tag `Line` with the item's `turn`; add `int coveredFrom(const QStringList &savedLines, const QJsonArray &items)` — the first ✦ row of the saved text that prefix-matches a prompt item (last matching turn), so the pane knows which turns the file still covers. Pure, unit-tested.
2. `src/Pane.h` / `src/PaneSession.cpp`: on the two restore paths (relay `openSessionRow`, guest resume — not forks), when the file replay is queued also send `conversation_get` (`resume-fill-N`). Hold the file replay until that answer arrives (5s timeout → replay without fill); when it arrives, print `render()` rows for turns < coveredFrom between new rules (`— transcript of the earlier turns the saved text no longer covers —`), then flush the file replay.
3. `backend/relay_core/conv_index.py`: `conversation()` keeps the newest `limit` entries (`ORDER BY turn DESC, seq DESC` then reversed), matching `clampScrollback`'s newest-kept rule everywhere else.
4. Tests: `tests/transcriptreplay_test.cpp` (turn tags, coveredFrom matching, duplicate-prompt last-match), `backend/tests/test_conv_index.py` (newest-kept ordering), verify against the real `5fa1…` data offline (coveredFrom → 8).

## Tasks

- [x] Measure the fault on the reported conversation (window starts mid-word; turns 1–6 gone).
- [x] `TranscriptReplay`: turn-tagged rows, `beforeTurn` fill rendering, `coveredFrom` window dating (prompt-row precise path; reply-fragment fallback for windows that lost their ✦ rows).
- [x] `Pane`: request the transcript alongside the file replay, hold the replay until the answer (5 s timeout), print the fill above it; new rule pair filtered on save.
- [x] `conv_index.conversation()`: newest-kept limit.
- [x] Tests: 4 new C++ cases, 1 new backend case; existing preview test re-run.
- [x] Probe against the live `5fa1…` data: `coveredFrom = 9`, fill draws turns 0–8.

## Execution Summary

Landed `4d6c210c` and `dff42e06`.

- `coveredFrom(savedLines, items)` (`src/TranscriptReplay.h`) dates the window: the first ✦ row the saved text still holds, prefix-matched against prompt items (last matching turn — "Continue" opened two turns of the measured conversation) is the exact boundary. Windows that have scrolled past even that — the reported one had: it opened at a turn's recap — are dated by replies, matched with all whitespace taken out of both sides so a wrap cut mid-word still counts; the newest turn found is the answer, so the fill over-covers at worst (a turn may repeat, once, above its own window) and never drops one.
- The restore paths (`setInitialState`, `openSessionRow`, guest resume) now ask for `conversation_get` whenever a saved text replays; `replayRestoredScrollback` holds the replay while the request is outstanding, the answer handler prints `render(items, maxRows, beforeTurn)` rows between `— transcript of the turns this conversation's saved text no longer covers —` rules above the replay, and a 5 s timer drops a worker that never answers so the replay is never stuck. The rule pair is in `restoreMarks()`/`dropRestoreMarks()`, so the fill does not stack across saves.
- `conv_index.conversation()` fetches newest-first and reverses, so the limit keeps the newest entries; the sessions preview and the no-file fallback both end where the conversation ended now.

Measured on the live conversation after the fix logic: `coveredFrom = 9`, fill draws turns 0–8 above a window that holds turn 9's recap onward. When the window is whole, nothing matches and the restore is byte-for-byte what it was.

One deviation from the plan: the plan's `coveredFrom → 8` probe was written when the conversation had 8 turns; by the time the matcher ran against it the conversation had grown to turn 10 and the window had slid to the turn-9 recap — which is what forced the reply-fragment fallback the plan did not have. Evidence: `docs/qa_evidence/2026-09-24-restored-transcript-fill/`.

## Tests

- `relay-transcriptreplay-tests`: 13 passed, 0 failed (4 new: `beforeTurnDropsThatTurnAndLater`, `coveredFromMatchesTheFirstPromptRow`, `repliesDateTheWindowWhenNoPromptRowSurvives`, `aRepeatedPromptAnswersItsLastTurn`).
- `tests.test_conv_index.IndexTests.test_conversation_limit_keeps_the_newest_entries` (new) and `.test_conversation_preview_highlights_and_filters_by_turn` (existing, guards the ordering change): OK.
- `scripts/relay-build --target relay`: builds; `land.py`'s verify gate built the exact tree of `4d6c210c` before it landed.
- Probe: `coveredFrom(5fa1 saved lines, 5fa1 items) = 9` on the conversation as it stands today (the card's original `= 8` was written before turns 9–10 landed); fill draws turns 0–8.
