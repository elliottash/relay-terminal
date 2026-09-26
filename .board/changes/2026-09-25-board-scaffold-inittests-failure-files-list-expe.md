---
id: 42G1
type: work
status: inbox
labels: [bug, board]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: 'Relay pane (card #NXN0 work), 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Board-scaffold InitTests failure: files list expects RELAY.md, scaffold writes AGENTS.md

## Issue
`tests/test_board_protocol.py::InitTests::test_the_owners_first_card_asks_first_and_lands_on_a_yes` fails on a clean export of `main` (verified 2026-09-25): the scaffold's `created[0]["files"]` no longer matches the expected list — the test expects the scaffold to record `RELAY.md` where the scaffold now records `AGENTS.md` (assertion at tests/test_board_protocol.py:2112 area). Board-scaffold territory, not related to card turns; left for whoever owns the scaffold.

> Owner asked nothing; found while running the backend tests for card #NXN0.
> — elliott · [session:74e8fa8794ea49cd8002041c4116b285](relay://session/74e8fa8794ea49cd8002041c4116b285) · 2026-09-25
