---
id: SDR1
type: work
status: inbox
labels: [bug, remote]
component: [remote]
rank: m
created: '2026-09-21'
source: 'Claude Code session on #SWPH, 2026-09-21: found by the hosted drive while grepping every frame the phone decrypted for machine paths'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-swph-hosted-drive/notes.txt], related: [SWPH, 0VT4, W5N2], github: null}
---
# The pane agent's `conversations` event carries `session_dir`, an absolute path, to `full` devices

## Issue

`docs/REMOTE-PROTOCOL.md` says no path crosses the wire. The #SWPH drive's path check (step 12)
passed for every `board_event`, and while making it the drive saw that the pane agent's
`conversations` event, forwarded to `full` devices, carries `session_dir`: an absolute path under
the owner's data directory. It predates #SWPH. The forwarding filter in `remote/wire.py` /
`remote/host.py` should strip it (the phone opens a conversation by the per-publish token in
`pane_state`, never by a path), with a test that greps a forwarded `conversations` for `/`.
