---
id: 43XK
type: work
status: inbox
labels: [bug, board]
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Board check: invalid status 'needs_qa_llm' and unknown thread kind 'landed'

## Issue
python3 scripts/relay-board.py check (2026-09-24, after the .board/ rename) reports 2 errors:

- changes/2026-09-24-sessions-pane-in-another-project-lists-the-la.md: bad_status: status 'needs_qa_llm' is not one of the allowed statuses (the allowed spelling is 'needs-qa-llm' with hyphens)
- threads/7QSK.md: bad_thread_entry: entry 20260924T131804Z-f2 has unknown kind 'landed'

Both were written by tooling after 2026-09-24 15:03 (neither exists in the pre-restart backup), so something writing board files uses a status spelling and a thread-entry kind the checker does not accept.
