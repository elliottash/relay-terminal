---
id: SCN9
type: work
status: executing
labels: [bug, switchboard, terminal]
assignee: agent
implemented_by: glm/glm-5.3
session: cf600dfa-e32c-4893-88f2-a0c65ba42ef6
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: pane b0371114 (glm session f1476), 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Terminal pane drops board_cards batches, so its card index stops at 400 rows and #IDs like #PH0N never link

## Issue
can you check, in the session in this pane: b0371114 why is #SAW4 and #PH0N not linked in the terminal? that seems like a bug

## Done means
- A terminal pane that receives a multi-batch board snapshot (a `board` event plus `board_cards` follow-ups) ends with **every** card in its `m_cardIndex`, so `#PH0N` and any other id past row 400 linkifies in prose exactly like ids in the first batch.
- A pane that misses a batch (arrival count below the snapshot's `cards_total`) notices and re-asks rather than holding a partial index forever.
- Failure recognised by: a test that feeds `relayMessage` a two-batch snapshot and asserts `lookupOutputCard` resolves a card that travelled only in the second batch; and the 658-card real board resolving `#PH0N` from a fresh pane.
