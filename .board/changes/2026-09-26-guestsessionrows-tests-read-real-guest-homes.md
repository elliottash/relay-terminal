---
id: 6MF1
type: work
status: executing
labels: [bug, sessions, tests]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:ashe-ethz-ch
session: dc713c52-45bc-4954-ba65-72ecb17516de
discovered_from: C0Q8
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low}
links: {plans: [], commits: [ca9c39575798], evidence: [], related: [], github: null}
---
# GuestSessionRows tests read real guest homes

## Issue
`tests/test_session_protocol.py::GuestSessionRows` sets a temporary HOME but inherits Relay guest-home and saved user-config environment variables. Reconciliation can scan this machine’s real Claude/Codex transcripts, causing failures and slow tests.

## Done means
Every `GuestSessionRows` test reads only the synthetic guest homes in its temporary directory, regardless of Relay guest-home environment variables in the parent process. The focused tests pass.

## Execution Summary
`GuestSessionRows` now overrides Relay guest-home and account roots so it reads only synthetic fixtures. Commit `ca9c3957`, queue job `19a3974b3341b358` (publication pending).

## Tests
- tests/test_session_protocol.py::GuestSessionRows::test_a_guest_session_is_listed_with_its_own_resume_command
- Manual run: `python3 -m pytest -q tests/test_session_protocol.py::GuestSessionRows` — 17 passed.
