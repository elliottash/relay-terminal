---
id: 95VZ
type: work
status: needs-verification
labels: [feature, switchboard, board, skills]
component: [worker, gui]
assignee: agent
implemented_by: anthropic/claude-fable-5-1
parent: 1QKM
blocked_by: [WFRA, MSJ0]
rank: zzzzzzzzzzzzzzzzzzzw
created: '2026-09-23'
verify: {artifact: code, primary: script, also: [ai-text], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: owner, Relay conversation, 2026-09-23
links: {plans: [], commits: [810302be], evidence: [tests/test_cases.py], related: [1QKM, BX7B, SJTR], github: null}
---
# A case ledger: every served case recorded with its server, who served it, cost, signal and verdict

## Issue
a card is built, a case is served. a program serves the case algorithmically; a skill serves it intelligently. [...] 2 definitely yes, think about a doctor or lawyer using relay to help with their case work.

[...] yes, document it, and lets build all the functionality, and we can experiment with how to phase in complexity without overwhelming the user

## Done means
- A case record exists as one JSON line per case in `<board>/cases.jsonl` (git-tracked, append-only, same locking as threads): `id`, `when`, `server` (skill id, program path, or `person`), `server_version` (skill file hash / commit), `served_by` (person | model signature), `card` (optional), `input` (a path or a one-line reference, never content), `cost` (tokens, seconds, money), `signal` (the verify primary mode and its result), `escalated` (bool + to whom), `verdict` (pass | fail | pending, who, revision), `confidential` (bool: the row then carries only ids).
- Rows are written by the worker at: the end of a turn that loaded a skill with a profile; a `board_try` answer; a verifier's `## Verdict`; a `board_move_card` to `done` or back; and an explicit `board_case` tool the agent calls for a person-served case ("log this: referee report, EJ, served by me, 3 h").
- `board_list` gains `cases: true` to return the last N rows filtered by server or card; each `skills_list` item that has a profile carries `cases`, `last_served`, `pass_rate_30` and `stale` (no passing case within the profile's `rot`). *Corrected 2026-09-24 under the owner's steer of 2026-09-23 — "ideally, most of this is just in the agent's work and the user doesn't see it directly" — so the Switchboard's Skills list shows nothing new on this card: the statistics are agent-facing data on the `skills_list` items, and no GUI changes.*
- The third person-served case of one `server`/shape inside 90 days prompts one line in the agent's reply: "three cases like this; build a server? (`/deliver`)"; no card is created automatically (owner, 2026-09-23).
- A `confidential: yes` profile writes rows without `input`, and `board_list cases` never returns them to a pane whose workspace is not the case's board.
- Tests: `tests/test_board.py` (append, lock, confidential row), `tests/test_board_tools.py` (each writer, `board_case`, the third-case line, filters). Failure shows as a skill turn with no row, or a confidential row carrying content.

## Plan
1. `backend/relay_core/cases.py`: the record (`new_record`, the twelve fields, `parse_cost`), `append` under the board directory's lock (the threads' discipline, one `O_APPEND` line), `read` with server/card/limit filters and a confidential switch, `stats` (cases, last_served, pass_rate_30, stale by `rot`), `third_case_hint`, `skill_version` / `program_version`, `verdict_from_text`.
2. Writers, each the smallest hook: `Agent._end_turn` → `BoardTools.record_turn_cases` for a turn that loaded a profiled skill (versions kept on `ToolContext.skill_versions`); `tryit_protocol.answer`; `## Verdict` through `board_update_card`; `board_move_card` to `done` (pass) or back a stage (fail); the new `board_case` tool beside `board_try`.
3. Readers: `board_list {cases: true, server, card}` with confidential rows only for the board's own workspace (`BoardTools.own_workspace`, the pane's `configure` workspace passed through `board_protocol._build`); `skill_manage.list_skills` adds the four statistics to profiled items; `BoardTools._qa_floor` passes `cases=` from the ledger to `qa_policy.apply`.
4. Tests: `tests/test_cases.py`; `CaseLedgerTests` in `tests/test_board_tools.py`; `CaseStatisticsTests` in `tests/test_skills.py`; one Try it answer test in `tests/test_tryit_protocol.py`.
5. Docs: protocol 19.22, BOARD-FORMAT §1 tree and §4, `deliver` SKILL.md, ARCHITECTURE Skills, `.gitattributes merge=union`. No GUI change, no `board_policy.md` change.

## Tasks
- [x] cases.py: record, append+lock, read, stats, third-case hint
- [x] writers: turn end, Try it answer, Verdict, move, board_case
- [x] readers: board_list cases, skills_list statistics, qa_policy counts
- [x] tests: test_cases.py, test_board_tools.py, test_skills.py, test_tryit_protocol.py
- [x] docs: protocol 19.22, BOARD-FORMAT, deliver, ARCHITECTURE

## Execution Summary
Commit 810302be. No GUI change (owner steer 2026-09-23: "ideally, most of this is just in the agent's work and the user doesn't see it directly").

- `backend/relay_core/cases.py` (new): the record (`new_record`, `FIELDS` in order, `parse_cost` for `{tokens, seconds, money}` or phrases like `3 h` / `$12`), `append` under the board directory's lock (`filelock.open_directory_lock`, the threads' discipline; one `O_APPEND` line, `merge=union` in `.gitattributes`), `read(server, card, limit, include_confidential)`, `stats` → `{cases, last_served, pass_rate_30, stale}` with `stale` from the profile's `rot` (low 90 d, medium 30, high 7, since the last passing case; no rot reads as low; a skill with no row is not stale), `third_case_hint` (exactly the third person-served case of one server in 90 days), `hint_line`, `skill_version` (sha256 of SKILL.md), `program_version` (path@HEAD), `verdict_from_text`. A confidential row is written without `input`.
- Writers: `Agent._end_turn` → `BoardTools.record_turn_cases` (one pending row per profiled skill the turn loaded, `served_by` the worker's signature, cost from the turn's usage and elapsed time, `card` the last card the turn wrote to; the skill's SKILL.md hash rides on `ToolContext.skill_versions` via `Agent._skill_version`); `tryit_protocol.answer` (a person row, verdict from the answer's first decisive word); `board_update_card` writing `## Verdict` (verdict from its first pass/fail word); `board_move_card` to `done` (pass) or from needs-verification / a QA lane / done back a stage (fail); the new `board_case` tool beside `board_try` (`server`, `served_by` default person, `cost`, `input`, `card`, `verdict`, `signal`, `escalated`), result `{case, when, server, served_by, cases, confidential, third_case_hint, hint?}`. A row about a card names the loaded profiled skill, else the server the ledger already names for that card, else `card:<ID>`. `board_update_card` / `board_move_card` results carry `case: <id>` when they wrote one. No event, no thread entry for a row.
- Readers: `board_list {cases: true, server, card, limit}` → `{cases, total, truncated, path, confidential_hidden}`; confidential rows only when `BoardTools.own_workspace()` (the pane's `configure` workspace, now passed through `board_protocol._build`, is the board's repository or inside it; the Board worker and tests count as its own). `skill_manage.list_skills` adds the four statistics to every item with a profile from the workspace's board ledger. `BoardTools._qa_floor` passes `cases=cases.verified_count(...)` to `qa_policy.apply`, so `ai_may_gate_after` / `sample_after` numbers are real thresholds (BOARD-FORMAT §4 no longer says "not yet").
- Docs: protocol 19.22 (shape, example row, writers, readers), BOARD-FORMAT §1 tree + §4, `deliver` SKILL.md one bullet on `board_case`, ARCHITECTURE Skills one sentence, `.gitattributes`. `board_policy.md` untouched.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_cases` — 15 tests: schema and field order, confidential row without input, cost phrases, refusals, versions, verdict word; append creates the file, trailing-newline repair, 6 threads × 20 concurrent appends all land whole under the lock, read filters (server, card, last N), confidential rows left out and bad lines skipped; stats (cases, last_served, pass_rate_30, stale by rot low/medium/high, no-rot, no-pass, no-row), pass rate over the last 30 decided, third-case hint only at the third and only for person-served rows.
- `PYTHONPATH=backend python3 -m unittest tests.test_board_tools.CaseLedgerTests tests.test_board_tools.SpecTests` — `board_case` logs and counts; the third case in 90 days carries the hint once; a confidential profile drops the input; refusals (no server, bad cost, unknown card, unknown arg, over-long input, read-only turn); a `## Verdict` writes a decided row naming the card's server; a verdict row reuses the server of the turn that served the card; a turn that loaded a profiled skill leaves a pending costed row and one that loaded none writes nothing; a move to done is a pass and back a stage a fail; `board_list cases` filters by server/card/limit and hides confidential rows from another workspace; the QA floor counts passing cases from the ledger (`ai_may_gate_after: 2`).
- `PYTHONPATH=backend python3 -m unittest tests.test_skills.CaseStatisticsTests` — profiled items carry the four statistics; unprofiled items none; no board reads as 0 / not stale.
- `PYTHONPATH=backend python3 -m unittest tests.test_tryit_protocol` — the Try it answer leaves a person-served row.
- Whole targeted suites: `tests.test_cases tests.test_board_tools tests.test_skills tests.test_tryit_protocol tests.test_qa_policy` = 418 OK; `tests.test_agent tests.test_routing_thinking_skills tests.test_board_protocol` = 257 OK; the same on a clean export of 810302be.
