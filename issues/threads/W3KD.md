<!-- relay:entry 20260920T154240Z-b1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 15:42
Reproduced against this board through the board tools: creating a `type: alias` card succeeds and
lands in `issues/aliases/`, then `board_move_card` on it answers
`{'error': "unknown tab 'aliases'; this board has: bugs, design, features, marketing, planning"}`.
The cause is the missing `alias` branch beside the `plan` and `memory` ones at
`backend/relay_core/board_tools.py:2038-2039`, repeated at `:2482`.

Filed under policy rule 6 while auditing the non-work card types; not claimed.
