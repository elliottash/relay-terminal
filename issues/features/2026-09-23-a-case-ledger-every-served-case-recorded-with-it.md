---
id: 95VZ
type: work
status: planned
labels: [feature, switchboard, board, skills]
component: [worker, gui]
parent: 1QKM
blocked_by: [WFRA, MSJ0]
rank: zzzzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [1QKM, BX7B, SJTR], github: null}
---
# A case ledger: every served case recorded with its server, who served it, cost, signal and verdict

## Issue
a card is built, a case is served. a program serves the case algorithmically; a skill serves it intelligently. [...] 2 definitely yes, think about a doctor or lawyer using relay to help with their case work.

[...] yes, document it, and lets build all the functionality, and we can experiment with how to phase in complexity without overwhelming the user

## Done means
- A case record exists as one JSON line per case in `<board>/cases.jsonl` (git-tracked, append-only, same locking as threads): `id`, `when`, `server` (skill id, program path, or `person`), `server_version` (skill file hash / commit), `served_by` (person | model signature), `card` (optional), `input` (a path or a one-line reference, never content), `cost` (tokens, seconds, money), `signal` (the verify primary mode and its result), `escalated` (bool + to whom), `verdict` (pass | fail | pending, who, revision), `confidential` (bool: the row then carries only ids).
- Rows are written by the worker at: the end of a turn that loaded a skill with a profile; a `board_try` answer; a verifier's `## Verdict`; a `board_move_card` to `done` or back; and an explicit `board_case` tool the agent calls for a person-served case ("log this: referee report, EJ, served by me, 3 h").
- `board_list` gains `cases: true` to return the last N rows filtered by server or card; the Switchboard's Skills list (from `skills_list`) shows per skill: cases served, last served, pass rate over the last 30, and **stale** when the last verified case is older than the profile's `rot` allows.
- The third person-served case of one `server`/shape inside 90 days prompts one line in the agent's reply: "three cases like this; build a server? (`/deliver`)"; no card is created automatically (owner, 2026-09-23).
- A `confidential: yes` profile writes rows without `input`, and `board_list cases` never returns them to a pane whose workspace is not the case's board.
- Tests: `tests/test_board.py` (append, lock, confidential row), `tests/test_board_tools.py` (each writer, `board_case`, the third-case line, filters). Failure shows as a skill turn with no row, or a confidential row carrying content.
