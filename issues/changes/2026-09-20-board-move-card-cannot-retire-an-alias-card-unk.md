---
id: W3KD
type: work
status: inbox
labels: [bug, switchboard]
assignee: ''
rank: m
created: '2026-09-20'
source: 'found while auditing card types for the owner, 2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# board_move_card cannot retire an alias card: "unknown tab 'aliases'"

## Issue
Found while auditing how completely the non-work card types are implemented (owner asked,
2026-09-20). Not a request — a measured fault, filed under policy rule 6.

`board_move_card` computes the destination folder with a branch per card type:

```python
category = (B.PLAN_FOLDER if card.type == "plan" else B.MEMORY_FOLDER if card.type == "memory"
            else self._category_for_tab(tab))
```

`backend/relay_core/board_tools.py:2038-2039`. There is a branch for `plan` and one for `memory`,
and **none for `alias`**, so an alias card falls through to `_category_for_tab("aliases")`, which is
strict (`board_tools.py:1562-1568` → `board.category_for_tab`, `board.py:1537-1550`) and knows only
the configured tabs.

Measured, on this board:

```
MOVE ALIAS: {'error': "unknown tab 'aliases'; this board has: bugs, design, features, marketing, planning"}
```

So an alias card can be created through the board tools (`board_create_card {type: "alias"}` works,
because `Card.expected_folder` overrides the tab at `board.py:825-826`) but can never be moved,
which means **it can never be retired**. `ALIAS_STATUS_FOLDER = {"active": "", "retired": "archive"}`
(`board.py:183-184`) declares a retired state that no tool can reach. Only editing the file by hand
does it.

`board_tools.py:2482` has the same missing branch.

## Acceptance
`board_move_card` on a `type: alias` card moves it between `issues/aliases/` and
`issues/aliases/archive/` the way it already does for `plan` and `memory`, and a test covers the
active→retired→active round trip.

## Tasks
- [ ] the `alias` branch at `board_tools.py:2038` and at `:2482` <!-- t:a1 -->
- [ ] a round-trip test beside the memory one in `tests/test_board_tools.py` <!-- t:a2 -->
