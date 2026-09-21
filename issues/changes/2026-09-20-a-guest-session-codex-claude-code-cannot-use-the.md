---
id: 4NXH
type: work
status: needs-verification
assignee: codex
labels: [bug, switchboard, guest]
rank: zzzzzzzzzzzzzzi
created: '2026-09-20'
source: 'card #1V4F (Switchboard agent, 2026-09-20)'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-4nxh-guest-board-tools/], related: [1V4F, GT7X], github: null}
---
# A guest session (Codex/Claude Code) cannot use the Switchboard's board_* tools

## Issue
couldnt access tools:
The Switchboard board_* tools were not available in this session, so I could not append commit links or move #VWSD to needs-verification.
session: bb2d13c855b94b2c87c91ced1e437da6

## Findings
This is not a code regression: **a guest has no `board_*` tools by design today**, so the observed behaviour is the design showing.

- `docs/SWITCHBOARD-DESIGN.md` §4.10: *"A guest has no `board_*` tools on either route, so the brief says where the card and its thread live and what to do without them."* The Verify brief still names `board_update_card` / `board_move_card` for the verdict, which is what the session in this Issue hit.
- The board tools live in `backend/relay_core/board_tools.py` and are reached through the worker's own agent loop (`Agent.ask`); a guest harness turn never gets them — the protocol says Relay's tools are not offered to a guest ("it has its own").
- A guest CLI writing to a card is, today, only the `implemented_by`/`verified_by` argument the guest types (`board_tools.py:242`, "a guest CLI writing through the bridge") — there is no tool call, and the worker cannot stamp for it.
- The one bridge that exists, `backend/relay_core/guest_bridge.py` (protocol 26.5, Claude only), carries the IDE tools (`openDiff`, …) and no board tools. Generic MCP is #SSRQ, still deferred.
- So the fix is either to give the guest the board tools (an MCP surface the harness hands to Codex/Claude Code, guardrails unchanged) or to stop the briefs asking a guest for board calls and have Relay do the writes. That is the question below.

## Decisions
- "yes, i agree. and i am open to building SSRQ simultaneously" — go with option (a): serve Relay's board tools to the guest harness over a small Relay-owned, pre-trusted MCP surface (read + `board_comment` / `board_update_card` / `board_move_card`; no `board_create_card`), reusing the existing `board_tools.py` guardrails. Built as its own surface, not inside #SSRQ; #SSRQ (generic MCP server support) may proceed in parallel. Size: *large*.

## Plan

**Goal**
Give Relay-managed Codex and Claude Code harness sessions the five agreed tools: `board_list`, `board_read`, `board_comment`, `board_update_card`, and `board_move_card`. A guest can read its assigned card, append evidence and a verdict, and move it through the existing verification gates. Calls use the live pane's policy and identity. This is the Relay-owned MCP server; #SSRQ is the separate client for third-party MCP servers and is not a dependency.

**Findings**
- `backend/relay_core/guest_harness_provider.py`: `start_provider` starts the child before `HarnessProvider.bind` receives its Agent; `complete` forwards prompt/attachments but ignores the supplied tool definitions. Merely forwarding that list cannot give either external CLI callable tools.
- `backend/relay_core/guest_harness_codex.py`: `start` launches an app-server; `_start_thread` constructs start/resume/fork configuration. `backend/relay_core/guest_harness_claude.py`: `_argv`, `start`, and `_relaunch` own child configuration and restarts. These are the integration points, alongside the contract in `guest_harness.py` and provider setup in `session_protocol.py`.
- `backend/relay_core/board_tools.py`: `tool_specs`, `run`, `ToolContext`, card scopes, write budgets, and verification gates already own board behavior. `backend/relay_core/agent.py` adds checks in `_prepare` before `_execute` calls `board.run`; bypassing `_prepare` would miss pane plan-mode and deferred-tool rules. `Agent.ask` sets session context and begins the board turn.
- `backend/relay_core/guest_bridge.py` is a GUI-wide Claude IDE WebSocket service with pane discovery by process/path. It is not the transport to extend for a pane-owned board capability.
- `backend/relay_core/board.py` generates unconditional guest/file-edit instructions. `issues/POLICY.md` and guest briefs need capability-aware wording, or connected guests will still be told they have no tools.
- Local CLI help confirms Claude's `--mcp-config` and Codex's configuration overrides; exact MCP startup/readiness behavior, especially resumed threads, must be pinned with adapter fixtures and a real discovery probe during implementation.

**Steps**
1. Define a small optional bridge descriptor in `guest_harness.py`, accepted by both adapters and the fake harness. Keep probes/key tests bridge-free. Use a reserved MCP server name, `relay_board`, with exactly the five tools above; derive descriptions/input schemas from `BoardTools.tool_specs()` rather than copying them. Filter exposure against actual board availability. No create, claim, cleanup, app, shell, or general Relay tool forwarding.
2. Add `backend/relay_core/guest_board_bridge.py`: an in-worker dispatcher bound to this pane, plus an MCP stdio proxy child launched by each guest. The proxy talks to the worker over an ephemeral Unix socket in an owner-only runtime directory, using a random per-harness capability. It never constructs its own BoardTools or edits card files. Implement initialize/initialized, tools/list, tools/call, ping and bounded error handling; stdout is protocol-only. Verify the supported MCP protocol versions against both installed clients and record fixtures rather than borrowing the IDE bridge's version assumptions. Use process-local configuration; leave users' MCP files and other servers intact.
3. Wire bridge lifetime before harness startup, then bind its dispatcher when the provider receives the Agent. Discovery may run before binding; execution must return an explicit unavailable error until a live turn exists. On `complete`, attach the current turn, cancellation and emitted events, and revoke call access in `finally`. Run calls through the same prepare/execute checks as native calls, without recursive provider completion or a second board `begin_turn`. Resolve the existing deferred board-tool group explicitly for this allowlisted surface so a guest never needs an unexposed `load_tools` call. Serialize calls against turn teardown and reject calls for ended/replaced turns. Preserve native structured refusal codes and server-owned session/model/pane attribution; never use `by_owner=True` or identity from guest arguments.
4. Configure the stdio server in Codex's app-server/thread startup overrides and Claude's launch-time MCP configuration. Prove discovery before the first model turn; preserve the configuration on start, resume, fork, model/effort restarts and provider replacement. Ensure the installed launcher can import the proxy module as well as a source checkout. Close proxy/socket resources on startup failure, Stop, pane close, provider switch and worker death as appropriate; Stop revokes the turn but permits a later turn on the surviving harness. Unsupported or failed discovery gets a clear pane notice and the existing file fallback, not a false claim that tools are connected.
5. Integrate call visibility with the existing guest `_Turn` and adapter MCP events: one visible call/result and one audit mutation per invocation. Deduplicate transport retries by bridge generation plus request id and payload; reject reuse with changed arguments. Never automatically replay an ambiguous write after reconnect. A cancelled request not yet dispatched does not write; a write already committed remains committed and is reported, not rolled back or retried blindly. Protect against overlapping calls and old credentials crossing pane/workspace/provider boundaries.
6. Make the guest opening context and generated policy prefer the namespaced bridge tools when present, retaining file instructions for unmanaged CLIs or failed connections. Describe the five-tool limit honestly: automatic Execute/Verify flows must retain Relay's claim ownership; an ordinary guest needing a claim/new card still follows the established fallback for those unsupported operations. Do not add tools beyond the recorded decision. Update `docs/AGENT-SESSIONS-PROTOCOL.md` §29.3 and `docs/SWITCHBOARD-DESIGN.md` guest sections, plus relevant architecture notes. Keep generic server discovery, imports and third-party trust configuration on #SSRQ.
7. Add the targeted coverage below, run it, and drive both real guests on a disposable board. Record evidence under `docs/qa_evidence/YYYY-MM-DD-4nxh-guest-board-tools/`. Land implementation with an execution summary, actual test results and QA checklist, then move #4NXH to needs-verification.

**Risks**
- Startup precedes Agent binding and guest calls arrive while `complete` is waiting. Avoid a callback queued to that blocked thread; use a serialized worker-side dispatch lane with explicit turn-lifetime synchronization. Do not hold a lock across a guest response wait.
- Namespaced MCP tool names must be mapped consistently in discovery, prompts and rendered tool events. Policy text must not override successful discovery with the old unconditional “no tools” assertion.
- The five-tool scope leaves creation/claiming outside the bridge. Preserve current fallback and Relay-owned action claims; broader parity is follow-up scope, not an implied expansion of the owner's decision.
- A local capability isolates accidental cross-pane access; it is not a sandbox against another process with the same user's filesystem privileges. Guests already have file access. Keep tokens out of transcripts and persisted sessions and rotate on replacement.
- #SSRQ can share narrowly useful schema/transport helpers, but its client lifecycle and third-party trust rules must not gate this server. No additional owner decision is needed for this plan.

**Verify**
- New `tests/test_guest_board_bridge.py`: real proxy/worker round trips on temporary sockets and boards; exact allowlist/schema mapping; malformed/oversized messages; unavailable board; unknown tool; invalid capability; cross-pane isolation; structured board refusals; duplicate requests; shutdown/cancellation and overlapping-call races. No live paid model calls in automated tests.
- Extend `tests/test_guest_harness_codex.py`, `tests/test_guest_harness_claude.py`, `tests/test_guest_harness_provider.py`, and `tests/guest_harness_fake.py`: assert launch/config wiring, discovery/readiness, binding order, start/resume/fork/relaunch, failed startup cleanup, no bridge for probes, and one emitted tool result per request.
- Extend relevant cases in `tests/test_board_tools.py` and `tests/test_board_turns.py`: guest read-only/plan-scope refusal, write-budget preservation, deferred-group handling, assigned-card ownership, worker-stamped attribution, successful evidence/verdict/move, and refusal when the existing verification gates are unsatisfied. Add policy-generation coverage beside the existing board scaffold tests.
- Run `PYTHONPATH=backend python3 -m unittest tests.test_guest_board_bridge tests.test_guest_harness_codex tests.test_guest_harness_claude tests.test_guest_harness_provider tests.test_board_tools tests.test_board_turns`, plus the specific policy-generation cases changed. Run `python3 scripts/relay-board.py check` before landing.
- Manual acceptance, separately for Codex and Claude: from a Relay pane discover the five tools; read a disposable assigned card; comment, record evidence/verdict and move it using MCP; verify one thread entry with the right pane/session/model and the board refresh. Repeat after resume and model change, exercise read-only refusal, Stop and a subsequent turn, and confirm a separate pane cannot reach this bridge. Capture tool events and board/thread output. Also check an unmanaged CLI still receives usable fallback instructions.

## Tasks
- [x] Implement pane-owned MCP dispatch and lifecycle. <!-- t:b1 -->
- [x] Wire both harnesses and capability-aware instructions. <!-- t:b2 -->
- [x] Verify targeted tests and real-client discovery; record evidence. <!-- t:b3 -->

## Execution Summary
Implemented the five-tool `relay_board` MCP server for managed Codex and Claude harnesses, with a private pane capability, native Agent preparation/dispatch, turn revocation, request deduplication, cancellation and cleanup. Both adapters preserve configuration across resume/fork/relaunch. Board actions use native labels; dispatch refreshes the actual model for first-turn Claude attribution. Generated policy prefers discovered tools and retains the file fallback.

Evidence: `docs/qa_evidence/2026-09-21-4nxh-guest-board-tools/NOTES.md`, with separate Codex and Claude event/audit captures. Both real clients used all five tools and rediscovered after resume.

## Tests
- `tests/test_guest_board_bridge.py`
- `tests/test_guest_harness_codex.py`
- `tests/test_guest_harness_claude.py`
- `tests/test_guest_harness_provider.py`
- `tests/test_board_tools.py`
- `tests/test_board_turns.py`
- `tests/test_board.py::PolicyFileTests`
- manual: `docs/qa_evidence/2026-09-21-4nxh-guest-board-tools/NOTES.md`

## QA checklist
- [ ] In a managed Codex pane and a managed Claude pane, read/comment/update/move a disposable card; confirm one visible call/result and correct model/session attribution.
- [ ] Resume, change model/effort, Stop, then send a new turn; confirm tools remain usable and stopped queued writes do not execute.
- [ ] Confirm plan/read-only and verification gates refuse prohibited writes; confirm an unmanaged CLI still has usable file instructions.
