---
id: S7GX
type: work
status: executing
labels: [bug, ssh, terminal, guest]
assignee: codex
priority: 2
rank: mssh1
created: '2026-09-22'
source: 'Owner in Relay, 2026-09-22; delivery follow-ups to #SHPA'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-ssh-parity/], related: [SHPA, S5SH, S7KC, S7CX], github: null}
---
# Guest agents can execute commands and use files over the active SSH connection

## Issue
great, add these issues to a card or cards so we can then deliver them

and look here:

7c0c259171d44c7cb4a43bb113a34d08

the agent couldnt run commands in the ssh terminal?

## Planning notes
Confirmed in session `7c0c259171d44c7cb4a43bb113a34d08`, linked Codex rollout `01a0c94c-cfb5-71f2-a4cd-6f483c56739d`: Relay supplied an idle, shareable SSH session on filly and instructed `run_command(host="filly")`. Guest tool discovery returned only nine board/delegation/todo tools. No remote command was attempted. Native remote execution exists; guest exposure does not.

`backend/relay_core/guest_board_bridge.py` restricts ALLOW to BOARD_ALLOW | DELEGATION_ALLOW. `guest_harness_provider.py` forwards native remote-context instructions; `remote_session.py` describes tools the guest lacks. The visible-terminal control path is distinct from noninteractive execution and must preserve its existing handoff rules.

First delivery priority. Can begin independently of #S7KC; coordinate visible-terminal completion with that card and destination validation with #S7CX. Do not substitute a new SSH login or a local command for a missing remote tool.

## Done means
- Codex and Claude guest tool discovery exposes supported remote command and file operations for the pane's active SSH session, and context advertises only callable capabilities.
- A guest asked “what are the big files here” inspects a controlled remote fixture through the existing authenticated connection and returns measured results, without requiring the user to copy commands.
- Remote operations retain host/cwd binding and existing file protections; local operations remain explicitly local. Closed, changed, or unavailable connections produce accurate errors and never silently run locally or reconnect.
- Visible-terminal handoff is available to guests when the pane permits it, with result/exit reporting, cancellation and existing input-control restrictions; it is not confused with noninteractive host execution.
- Recorded guest-harness integration evidence covers both supported guest adapters, capability changes between turns, and a real SSH command; native tools and board/delegation behavior still work.

## Tasks
- [x] Expose session-bound remote command/file operations through the guest bridge and route them through existing executors. <!-- t:gp -->
- [x] Generate guest remote context from actual available tools; cover reconnect, disconnect, cancellation and stale calls. <!-- t:0y -->
- [ ] Connect permitted visible-terminal handoff and completion to guest calls, coordinating #S7KC. <!-- t:t9 s=in-progress -->
- [x] Add bridge/adapter regression tests and live-drive the reported large-files request against remote fixtures. <!-- t:dk -->

## Plan
**Goal:** Deliver the existing Done means with live evidence.
**Findings:** See Planning notes and #SHPA; the audited paths are unchanged.
**Steps:** Expose remote executor and terminal handoff tools through the shared guest MCP bridge, preserving native preparation/execution and turn capabilities. Enforce shared-socket-only execution and test both guest adapters, MCP calls, cancellation, remote failures and real SSH.
**Risks:** Shared checkout; preserve other work. Remote and local command ownership must remain separate; do not route stale remote requests locally. No permanent remote shell startup changes.
**Verify:** Targeted regression tests plus isolated Xvfb/real localhost SSH; record precise limits of guest and platform testing. Independent verifier reviews acceptance and live evidence after implementation.

## Execution Summary
Exposed stable, mandatory-host remote command/file schemas plus bounded job polling, cancellation and permitted terminal handoff through the guest MCP bridge. All calls retain native preparation/execution policy. Missing/changed sessions refuse execution, and ProxyCommand=false prevents new-login fallback after control-socket loss. Corrected guest-facing host guidance. Independent real MCP/SSH evidence covers both adapter mappings and the large-file fixture query; GUI handoff completion is delivered with #S7KC.

## Tests
`tests/test_guest_board_bridge.py`
`tests/test_ssh_remote.py`
`PYTHONPATH=backend:. python3 -m unittest tests.test_guest_board_bridge tests.test_ssh_remote -q` — 61 passed.
manual: docs/qa_evidence/2026-09-22-verify-S7GX/README.md — real MCP transport and SSH commands/files through both adapter mappings; no paid model CLI launched.
