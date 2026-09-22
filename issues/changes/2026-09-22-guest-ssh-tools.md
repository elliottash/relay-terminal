---
id: S7GX
type: work
status: needs-verification
labels: [bug, ssh, terminal, guest]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
priority: 2
rank: mssh1
created: '2026-09-22'
source: 'Owner in Relay, 2026-09-22; delivery follow-ups to #SHPA'
links: {plans: [], commits: [66bdab9389882388ae425d10510e5864a33fcbf1, 5e07a8d640b283d58e5b48e89e55ea9bea663a33], evidence: [docs/qa_evidence/2026-09-22-ssh-parity/, docs/qa_evidence/2026-09-22-verify-S7GX/, docs/qa_evidence/2026-09-22-verify-S7KC/build6/, docs/qa_evidence/2026-09-22-ssh-delivery/], related: [SHPA, S5SH, S7KC, S7CX], github: null}
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
- [x] Connect permitted visible-terminal handoff and completion to guest calls, coordinating #S7KC. <!-- t:t9 -->
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
manual: docs/qa_evidence/2026-09-22-verify-S7GX/README.md

### Check 2026-09-22 11:19
- passed · unittest:tests.test_guest_board_bridge — tests/test_guest_board_bridge.py passed for this revision on spark-dcc9, 2026-09-22T15:19:41Z
- passed · unittest:tests.test_ssh_remote — tests/test_ssh_remote.py passed for this revision on spark-dcc9, 2026-09-22T15:19:41Z
- not-applicable · manual:docs/qa_evidence/2026-09-22-verify-S7GX/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-verify-S7GX/README.md
- notice · unittest:tests.test_ssh_remote — tests/test_ssh_remote.py: 1 of 46 are slow (test_handed_back_jobs_carry_the_host)
history: thread
## QA checklist
- [x] Independently verified backend implementation `66bdab9389882388ae425d10510e5864a33fcbf1`: SHA-256 values for all five scoped source/test files match the recorded live-drive revision.
- [x] 64 targeted tests passed: `PYTHONPATH=backend:. python3 -m unittest tests.test_guest_board_bridge tests.test_ssh_remote tests.test_attachments`.
- [x] Actual MCP stdio/Unix-socket bridge calls through both Codex and Claude adapter name mappings used a private authenticated localhost SSH master: remote cwd, output, exit status, file CRUD and measured large-file query passed.
- [x] Explicit-host requirement, wrong/no/unshareable session, ended turn, secret-file guards, missing/abandoned sockets, background job polling and SSH-job cancellation passed; disconnected sockets did not start a fresh login.
- [x] Independent injections after genuine preparation proved cancellation and changed-session guards refuse before execution. Native board and per-turn terminal capability regression tests passed. Previously reported misleading missing-host guidance is fixed.
- [ ] Visible-terminal GUI completion/input-control lifecycle remains with the coordinated #S7KC drive; this verifier did not run a build, GUI or live paid Codex/Claude model session.

- [x] Independent GUI verifier a2 completed the remaining handoff check: real localhost Bash terminal visibly executes the handed-off command and reports exact HANDOFF_STDOUT with exit 1 before disconnect; read/stdin control and Ctrl+C were separately live-driven. Evidence: `docs/qa_evidence/2026-09-22-verify-S7KC/build6/12-terminal-handoff.png` and `build6/handoff-events.jsonl`.

## Verdict
**Backend scope passes independent verification.** Evidence: `docs/qa_evidence/2026-09-22-verify-S7GX/README.md`, committed as `56e73a8418126ae627cbd1b768e8410c21a1422f`. Both adapter naming mappings and actual MCP transport were exercised with real SSH; the provider fixture avoids model calls. Worker live-update routing was reviewed statically, with executor session-change behavior tested independently. Cancellation evidence establishes stopping the SSH job, not every possible descendant remote process. This is not full-card GUI sign-off: complete the remaining visible-terminal handoff verification with #S7KC before closing the card. Card status is unchanged.

Independent GUI addendum (a2): **the earlier outstanding visible-terminal handoff check now passes.** The prior backend verifier’s no-GUI limitation describes that earlier session; a2 ran the separate GUI/real-SSH drive. Combined backend and GUI evidence does not include paid model CLIs, all descendant-process cancellation cases, or two physical hosts. Card status unchanged.
