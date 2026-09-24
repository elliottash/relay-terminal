---
id: SCN9
type: work
status: needs-verification
labels: [bug, switchboard, terminal]
assignee: agent
implemented_by: glm/glm-5.3
session: cf600dfa-e32c-4893-88f2-a0c65ba42ef6
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: pane b0371114 (glm session f1476), 2026-09-24
links: {plans: [], commits: [], evidence: [issues/changes/2026-09-24-terminal-pane-drops-board-cards-batches-so-its-c.md § Tests (unit tests PASS; commit 6fd0c6b4 verified on a clean export)], related: [], github: null}
---
# Terminal pane drops board_cards batches, so its card index stops at 400 rows and #IDs like #PH0N never link

## Issue
can you check, in the session in this pane: b0371114 why is #SAW4 and #PH0N not linked in the terminal? that seems like a bug

## Done means
- A terminal pane that receives a multi-batch board snapshot (a `board` event plus `board_cards` follow-ups) ends with **every** card in its `m_cardIndex`, so `#PH0N` and any other id past row 400 linkifies in prose exactly like ids in the first batch.
- A pane that misses a batch (arrival count below the snapshot's `cards_total`) notices and re-asks rather than holding a partial index forever.
- Failure recognised by: a test that feeds `relayMessage` a two-batch snapshot and asserts `lookupOutputCard` resolves a card that travelled only in the second batch; and the 658-card real board resolving `#PH0N` from a fresh pane.

## Execution Summary
Landed in `6fd0c6b4` (verified on a clean export of the exact tree: `relay` builds; the two pre-existing harness-link failures in the shared working tree are another session's uncommitted `panedir` work, not in these paths).

- New `relay::board::IndexFeed` in `src/BoardModel.{h,cpp}`: owns the terminal pane's card index and its chunk bookkeeping. `apply()` handles `board` (reset + the snapshot's announced `cards_total`), `board_cards` (rows patched in exactly as upserts — the batches a pane used to drop), and `board_changed` (upserts/removals, unchanged behaviour). When a snapshot's last batch lands short of its announced total it raises `needsRefetch()` **once per snapshot**; `reset()` and a fresh `board` clear the bookkeeping, so a board that still cannot deliver its rows keeps what landed rather than loop.
- `Pane::handleBoardEvent` (`src/Pane.h`) delegates the three index events to the feed and, on `needsRefetch`, clears `m_cardIndexAsked` and re-sends `board_open`. Member `m_cardIndex` changes type `Model → IndexFeed`; every other call site (`card`, `total`, `search`, `reset`) is forwarded, so the `#` picker and link lookups are untouched.
- With this, every card of the 658-card board reaches the index: `#PH0N` (and `#FR1C` etc., all past row 400) linkify like first-batch ids. The running GUI shows the fix after it is rebuilt and restarted.

## Tests
`tests/boardmodel_test.cpp`, target `relay-board-tests` (Qt 5.15.13, arm64), run via `scripts/relay-build --target relay-board-tests` then the binary:

- `chunkedSnapshotBatchesAllLandInTheIndex` — a three-batch `board_open` answer (`board` + two `board_cards`); a card that travelled only in the last batch (`PH0N`) resolves with its title, total is 4, and the feed does not ask again. **PASS**.
- `aSnapshotThatLandsShortAsksForTheBoardOnce` — a snapshot ending one card short of `cards_total` raises `needsRefetch` exactly once (no loop on a still-short repeat); `board_changed` upserts and removals keep working between snapshots; a fresh `board` restarts the bookkeeping; events the feed does not own (`board_activity`) are refused. **PASS**.

```
PASS : BoardModelTests::chunkedSnapshotBatchesAllLandInTheIndex()
PASS : BoardModelTests::aSnapshotThatLandsShortAsksForTheBoardOnce()
Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 1ms
```

Not run: the full `ctest --test-dir build` suites (owner's rule; targeted tests only). The fresh-pane `#PH0N` check of Done means is the Try-it for the verifier: open any pane with a board attached and hover/`Ctrl+click` `#PH0N` in prose — needs the rebuilt `build/relay` restarted.
