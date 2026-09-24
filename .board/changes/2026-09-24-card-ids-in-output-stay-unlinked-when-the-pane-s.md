---
id: QJXD
type: work
status: needs-verification
labels: [bug, switchboard]
assignee: agent
implemented_by: glm/glm-5.3
session: b758b75b-4825-4703-83cd-b80bd27897a0
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: 'a #XXXX that rendered plain in old output becomes a hoverable card link once the card exists on disk; unknown ids stay plain', sign_off: none, effort: low, stakes: rework, blast: capability}
source: terminal pane, 2026-09-24
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-qjxd-card-links/], related: [], github: null}
---
# Card ids in output stay unlinked when the pane's card index is stale

## Issue
can you look at the bottom here. 588e509d. why isnt that card code linked? ... yes, i agree with that, file the card and deliver

## Done means
- A `#XXXX` id that the pane does not know when it is drawn becomes a hoverable card link after the card is created on disk, without restarting the pane.
- An id that is not a card after a fresh read still stays plain text (typo-safe).
- Output without any unknown card id never sends a reread to the backend; unknown ids are throttled (at most one reread every few seconds per pane).
- Existing linked ids, paths and OSC-8 links are unchanged.

## Plan
**Goal.** A card-shaped id unknown to the pane triggers one throttled `board_refresh` through the pane's worker, so ids printed before a card existed turn into links without a restart.

**Findings.** `src/Pane.h` has `lookupOutputCard()` and sends `board_open` for its first index request. The worker's `board_refresh` returns `board_changed`, which `IndexFeed` applies. There is no `Pane::sendBoardRefresh` helper; the pane already sends worker messages through `send(...)`.

**Steps.**
1. Add a five-second unknown-card refresh gate in `src/OutputLinks.h` and use it in `Pane::lookupOutputCard`, recording the initial `board_open` against the same gate.
2. On an unknown card id in an attached, configured pane, send `board_refresh` when the gate is due. A known card does not reach the gate.
3. Test the gate and a card becoming linkable after an index update in `tests/outputlinks_test.cpp`.

**Risks.** Hover and tooltip paths repeatedly ask for links, so the gate must rate-limit every miss. An unattached pane must send nothing.

**Verify.** Build `relay` and `relay-outputlinks-tests` through `scripts/relay-build`; run `ctest -R '^outputlinks$'`.

## Tasks

- [x] Five-second unknown-card refresh gate in src/OutputLinks.h <!-- t:8z -->
- [x] Send board_refresh on an unknown card id in Pane::lookupOutputCard; record initial board_open against the same gate <!-- t:9x -->
- [x] Tests in tests/outputlinks_test.cpp (miss→one refresh, throttle window, link after refresh) <!-- t:8f -->
- [x] Build relay + outputlinks tests, land via scripts/land.py <!-- t:4m -->

## Tests
- `scripts/relay-build --target relay` — builds (2026-09-24 17:53, wrapper-stamped).
- `scripts/relay-build --target relay-outputlinks-tests` then `ctest --test-dir build -R '^outputlinks$' --output-on-failure` — **100% tests passed** (1/1), run against the committed test file (object newer than source; both new test functions present in the binary, checked with `-functions`).
- land.py verify build of the exact landing tree in `/tmp/claude-1000/land/qjxd-codex/verify/build`: `the exact tree builds`.
- Not run: full `ctest --test-dir build` (owner's rule; the suite currently also fails in another session's in-flight `tests/boardexecute_test.cpp` change, unrelated to this card).

## Execution Summary
Landed `aab23d78f3ed` on `main` via `scripts/land.py` (session `qjxd-codex`; the three `src/Pane.h` hunks were held as contested against three other live sessions' claims and confirmed only after checking each hunk against this session's snapshot diff — all three are this card's, no foreign code in them).

- `src/OutputLinks.h`: `relay::links::UnknownCardRefresh` — a five-second gate with `due(now)` / `requested(now)` and an `invalidate()` for tests.
- `src/Pane.h`: `lookupOutputCard()` sends `board_refresh` when a card-shaped id misses in an attached, configured pane and the gate is due; `requestCardIndex()` starts the pane-wide clock and records the initial `board_open` against the same gate so the first miss after startup is already inside a window. The reply path is unchanged: `board_changed` already flows into `IndexFeed`, which is what turns the next scan/hover into a link.
- `tests/outputlinks_test.cpp`: two tests, listed under Tests.

Not done here, on purpose: the pane still does not watch the board folder for its own sake — that is a bigger change (BoardRemote already watches it when a remote is attached). The miss-triggered reread covers the reported case with no new watcher.

Known and accepted: the refresh is per-pane, so a card id in one pane's old output links only in that pane; other panes refresh on their own misses.

## Try it
Staged 2026-09-24 18:00 by the implementing session; the app is running on Xvfb `:171` (`docs/qa_evidence/2026-09-24-qjxd-card-links/tryit/status.md`).

1. Open the staged instance (`DISPLAY=:171` — or view `docs/qa_evidence/2026-09-24-qjxd-card-links/tryit/01-before-card-on-disk.png` and `02-after-card-on-disk.png`; `staging-notes.md` says what each piece is).
2. In the pane's first line, hover `#B7XZ` — the id that was plain text when the card did not exist. Judge what happens against `expected.md` in the evidence folder.
3. `/tmp/claude-1000/tryit/qjxd/stop.sh` ends the staged instance; `stage.sh` restages it.

Evidence from the pass (screenshots, log, sandbox path) lives in `docs/qa_evidence/2026-09-24-qjxd-card-links/tryit/`.
