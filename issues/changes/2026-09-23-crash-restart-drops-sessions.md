---
id: N6R8
type: work
status: needs-verification
labels: [bug, sessions, gui]
assignee: codex
rank: m
created: '2026-09-23'
source: 'User report in Relay pane, 2026-09-23'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-crash-session-recovery/], related: [K6KP], github: null}
---
# A crash restart drops the saved conversation from restored panes

## Issue
i just had relay crash, and i restarted and none of my sessions recovered

## Done means
- A pane reopened after a crash keeps its saved conversation ID while a guest agent has not started yet.
- A subsequent layout save retains that ID until the conversation is loaded or definitively fails to load.
- A crash leaves recently saved terminal text for the reopened panes.

## Plan
**Goal:** preserve the conversation reference and recent terminal text through a crash restart.

**Findings:** The 12:44:18 SIGSEGV left the 12:44:25 restart with eleven panes but only one `session_id` in `windows.json` by 12:45. Ten agents were deferred until first prompt, so the layout writer serialized their empty new worker ID. Scrollback files were last written before the crash because they are saved on clean close.

**Steps:** retain the pending restore ID through deferred configure and resume; serialize it until loading finishes; checkpoint pane text periodically; verify in an isolated profile.

**Risks:** Saving text periodically adds I/O for many panes. Throttle the checkpoint interval and keep the existing atomic writer.

**Verify:** Build Relay; run a focused crash/restart drive that inspects layout IDs and text files without closing the original process cleanly.

## Execution Summary
The saved layout now uses a pane's pending restore ID until `state_loaded` or a restore failure, including while the guest harness waits for a first prompt. A separate 30-second timer checkpoints each pane's terminal text with the existing atomic scrollback writer. The isolated [crash drive](docs/qa_evidence/2026-09-23-crash-session-recovery/README.md) verified both after a forced kill and restart. The live crash's conversation files survived; six lost pane-to-conversation mappings were recovered from logged turn IDs, while four idle panes had no reliable logged mapping. The live layout was not modified.

## Tests
- `scripts/relay-build --target relay` — passed, build 2026-09-23.12H.09.
- `bash docs/qa_evidence/2026-09-23-crash-session-recovery/drive.sh` — passed both assertions in an isolated Xvfb/XDG profile.
- `git diff --check` — passed.
- `python3 scripts/relay-board.py check` — global check still reports 14 pre-existing errors in other cards/threads; no N6R8 error.
