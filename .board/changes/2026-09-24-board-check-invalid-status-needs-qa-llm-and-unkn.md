---
id: 43XK
type: work
status: needs-verification
labels: [bug, board]
assignee: agent
implemented_by: glm/glm-5.3
session: 40bcc267-38f3-4bf8-8da8-5987cc87fa4b
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'scripts/relay-board.py check reports 0 errors; 7QSK sits at needs-verification with a valid thread kind; the writer finding (hand-written files, no code path) is recorded', sign_off: none, effort: low, stakes: rework, blast: capability}
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Board check: invalid status 'needs_qa_llm' and unknown thread kind 'landed'

## Issue
python3 scripts/relay-board.py check (2026-09-24, after the .board/ rename) reports 2 errors:

- changes/2026-09-24-sessions-pane-in-another-project-lists-the-la.md: bad_status: status 'needs_qa_llm' is not one of the allowed statuses (the allowed spelling is 'needs-qa-llm' with hyphens)
- threads/7QSK.md: bad_thread_entry: entry 20260924T131804Z-f2 has unknown kind 'landed'

Both were written by tooling after 2026-09-24 15:03 (neither exists in the pre-restart backup), so something writing board files uses a status spelling and a thread-entry kind the checker does not accept.

## Done means
- `scripts/relay-board.py check` reports 0 errors (warnings about missing verify blocks are a separate sweep).
- #7QSK is at `needs-verification` — the status its land report records — spelled the allowed way.
- #7QSK's thread entry carries a valid kind; its text is untouched.
- The writer is identified with evidence: no code path can emit either fault.

## Execution Summary
Both faults came from **hand-written board files**, not from any writer in the repo:

- The `kind=landed` entry was appended by hand in commit `082b9243` ("card #7QSK: record landed sha 74ec27ce", 2026-09-24 09:18 EDT) — `Board.append_thread` raises `BoardError` on a kind outside `ENTRY_KINDS` (`backend/relay_core/board.py:1512`), so no checked path can emit it.
- #7QSK's card was **created** with `status: needs_qa_llm` (the folder spelling) in its first commit; `board_create_card` validates statuses, so this too bypassed the tools.

This matches the 2026-09-24 survey finding (on #QJXD's thread): sessions fall back to raw file writes, and nothing forces them back. `check` is the gate that caught both — making it fail loudly in pre-commit/CI is the follow-up worth taking.

Repairs applied:
- `board_move_card` #7QSK → `needs-verification` (the intent its land report records; the tool wrote the hyphenated spelling and would have refused the folder form).
- `.board/threads/7QSK.md`: entry `20260924T131804Z-f2` kind `landed` → `event` (attribute normalization only; entry text untouched).
- `check --fix` was not used: it only sorts interleaved threads, and neither error had a fixable path.

Verified: `python3 scripts/relay-board.py check` → `695 card(s) checked: 0 error(s), 1282 warning(s)`.

## Tests
- `python3 scripts/relay-board.py check` after the repairs: `695 card(s) checked: 0 error(s), 1282 warning(s)` — the two named errors are gone.
- No code changed for this card (repairs + finding only), so no new unit tests; the existing `append_thread` kind validation is the guard the writer bypassed.
