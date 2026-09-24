<!-- relay:entry 20260920T154240Z-b1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 15:42
Reproduced against this board through the board tools: creating a `type: alias` card succeeds and
lands in `issues/aliases/`, then `board_move_card` on it answers
`{'error': "unknown tab 'aliases'; this board has: bugs, design, features, marketing, planning"}`.
The cause is the missing `alias` branch beside the `plan` and `memory` ones at
`backend/relay_core/board_tools.py:2038-2039`, repeated at `:2482`.

Filed under policy rule 6 while auditing the non-work card types; not claimed.

<!-- relay:entry 20260920T162818Z-qf author=claude-opus-5 kind=progress -->
Fixed in passing while dropping the plan card type (#X7NB): the missing `alias` branch was in the
same two lines that carried `B.PLAN_FOLDER`, at `board_tools.py` `_move` and `_split`, plus
`board_create_card`. An alias card now goes to `issues/aliases/` and `issues/aliases/archive/`
whatever the tab, so it can be retired and brought back;
`tests/test_board_tools.py:MoveTests.test_an_alias_card_is_retired_and_brought_back` covers the
round trip and the live run is in docs/qa_evidence/2026-09-20-drop-plan-cards/.

Moved to needs-verification.

<!-- relay:entry 20260920T163815Z-f1 author=claude-code kind=note -->
### Claude Code · 2026-09-20 16:38
Removed a hand-typed `implemented_by: anthropic/claude-opus-5` from the front matter before
landing. Policy rule 5 and the `board_update_card` appendix both say never to type it — Relay
stamps it, and a value typed by hand is what makes the audit trail a lie. The implementer was an
Opus 5 subagent working the files directly, so nothing stamps it here; its own progress entry above
is the record instead.
