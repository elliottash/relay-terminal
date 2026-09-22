---
id: A11T
type: work
status: needs-verification
labels: [feature, remote]
component: [gui, remote]
milestone: beta
workstream: remote
assignee: codex
rank: m
created: '2026-09-21'
acceptance: the share dialog offers All tabs; an invite or meeting code made with it reaches every current pane and panes created later across tabs, while pane-only and whole-tab shares remain narrow
source: 'Codex in a Relay pane, 2026-09-21'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-21-share-all-tabs/'], related: [T4BS, PH0N], github: null}
---
# Share all tabs with one option

## Issue

add an "all tabs" option

## Done means

The sharing dialog offers **All tabs** beside the existing whole-tab scope. A link or meeting code
made with it includes every pane currently published by Relay and automatically includes panes
opened later in any tab. Turning the option off withdraws panes published only for that scope,
without widening ordinary pane or whole-tab invitations.

Failure is an all-tabs guest missing a later pane, an ordinary invite gaining unrelated panes, or
the option disrupting an existing whole-tab share.

## Plan

**Goal.** Add a desktop-wide multiplayer scope while preserving the existing pane and tab scopes.

**Findings.** `src/RemoteShare.*` owns the sharing dialog and process-wide publication state;
`src/RelayWindow.h` follows tab membership; `remote/guests.py` and `remote/host.py` persist and
enforce guest scope; `tests/test_remote_tab_share.py` already drives dynamic tab growth.

**Steps.**

1. Add an All tabs control and process-wide publication state, including panes opened later.
2. Represent the scope explicitly on invites and participants and grow/shrink it as panes appear
   and disappear, without changing ordinary or whole-tab behavior.
3. Cover current panes, future panes, persistence, and scope isolation with targeted tests; update
   the remote protocol and record implementer evidence.

**Risks.** A desktop-wide guest scope exposes more than a tab, so the UI must name it plainly and
the wire must never infer it from a normal tab identifier. Existing shared-checkout edits are
preserved through `scripts/land.py`.

**Verify.** Run `tests/test_remote_tab_share.py`, the relevant remote security cases, the Relay
build gate, and the board format check.

## Tasks

- [x] Add the desktop-wide scope to the dialog and pane publication <!-- t:A1 -->
- [x] Enforce and persist the scope in the sidecar <!-- t:B1 -->
- [x] Add targeted tests and protocol documentation <!-- t:C1 -->

## Execution Summary

The sharing dialog now offers **Share all tabs**. The selection publishes panes across every Relay
window, follows panes and tabs opened later, and mints links and meeting codes with an explicit
`all-tabs` dynamic scope. The sidecar persists and enforces that scope independently of ordinary
pane and whole-tab shares; `scope_end` withdraws only the selected dynamic scope.

Protocol documentation and implementer evidence are in
`docs/qa_evidence/2026-09-21-share-all-tabs/`.

## Tests

- `PYTHONPATH=. python3 tests/test_remote_tab_share.py`
- `python3 -m py_compile remote/guests.py remote/host.py remote/gui_host.py`
- `scripts/relay-build`
- `python3 scripts/relay-board.py check` (the repository has pre-existing errors; no #A11T findings after task-marker correction)
