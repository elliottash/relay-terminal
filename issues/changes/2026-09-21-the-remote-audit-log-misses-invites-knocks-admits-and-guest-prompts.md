---
id: AUDL
type: work
status: inbox
labels: [bug, remote]
component: [remote]
rank: m
created: '2026-09-21'
source: 'Claude Code session on #PH0N, 2026-09-21: found by the hosted drive `docs/qa_evidence/2026-09-21-ph0n-hosted-drive/`'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-ph0n-hosted-drive/audit.txt], related: [PH0N, W5N2]}
---
# The remote audit log records pairing and push subscriptions, but not invites, knocks, admits or guest prompts

## Issue

`docs/REMOTE-AND-MULTIPLAYER-DESIGN.md` §7 says the local audit log records "pairings, joins, role
changes, control handoffs, prompts submitted", and `remote/host.py` calls `audit.record` for
`invite_create`, `knock`, `admitted` and the guest-prompt decisions. In the hosted drive of
2026-09-21 (a full run with an invite, a knock, an admit from the phone and a guest prompt run
from the phone), the profile's `audit-2026-09.jsonl` holds 13 lines: `pair` and two
`push_subscribe`, nothing else (`docs/qa_evidence/2026-09-21-ph0n-hosted-drive/audit.txt`). Either
the calls do not run on the sidecar path the GUI uses (`remote/gui_host.py`), or the records go
to a file under a different root than the one the drive read. Reproduce with that folder's
`drive.sh`; `tests/test_remote_audit.py` (5 cases) does not cover the sidecar path.
