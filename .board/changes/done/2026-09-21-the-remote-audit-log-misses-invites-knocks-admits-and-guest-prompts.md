---
id: ADTR
type: work
status: done
labels: [bug, remote]
component: [remote]
assignee: codex
implemented_by: kimi/kimi-k3
verified_by: kimi/kimi-k3
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

## Done means
The GUI sidecar path records invite creation, knocks, admission and guest prompt decisions beside its device store. An integration regression demonstrates the recorded actions and identifies the cause of the reported missing evidence.

## Execution Summary
The report is superseded by d56c80aa's rerun: docs/qa_evidence/2026-09-21-ph0n-hosted-drive/audit.txt contains all 13 records, including invite_create, knock, admitted, guest_prompt and prompt_decided. Added a real-socket Sidecar integration regression, which passes; no audit implementation change was necessary.

## Tests
- `tests/test_remote_gui_host.py::AlwaysOnTests::test_sidecar_multiplayer_actions_reach_the_device_store_audit`
- `tests/test_remote_audit.py`
