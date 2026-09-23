---
id: GPA8
type: work
status: needs-verification
labels: [feature, guest, agent]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [423326d77f5ba247f8dc3223c34aa1f8f8118612], evidence: [docs/qa_evidence/2026-09-23-GPA8-guest-tool-parity/verification.md], related: [XP7N, 4NXH, AG7R], github: null}
---
# Close guest agent tool parity gaps

## Issue
and then after that's done, I want to look for other tools that are missing for the gas agencies, cuz I I thought we had parity, or at least I wanted that.

## Done means
- Relay-managed Codex and Claude guests can use the Relay capabilities their pane's native agent can use, through the same policy checks and UI events.
- Any deliberate exceptions are listed with their owner decision and an equivalent guest path where one exists.
- A parity test compares the native tool catalog with the guest bridge and fails when a new Relay-only capability is added without an explicit decision.

## Planning notes
Audited `Agent.tools()`, `guest_board_bridge.Bridge.specs()`, and their source catalogs after #XP7N added `write_plan` and `exit_plan_mode`.

- Board: 12 native tools, 5 bridged. Missing `board_create_card`, `board_claim`, `board_import_items`, `tests_check`, `tests_run`, `board_signals`, `board_try`. #4NXH explicitly chose five tools and excluded `board_create_card`; this needs a new decision before widening that part.
- Relay app: 14 native `app_*` tools, only `app_user_memory` bridged. The other 13 cover options, actions, panes, cross-pane prompts, names, sessions search, open, changes and undo. #AG7R defines the native app action guardrails; guest calls should reuse them.
- Session: `session_info` and `activity` are native only.
- Conditional native tools: `set_keybinding` and `type_into_program` are absent from the bridge. Guest harnesses have their own shell, file, skill and question tools, so `run_command`, `ask_user`, `load_skill` and `read_skill_file` require a capability comparison rather than a name-only comparison. The bridge already covers SSH command/file tools, terminal handoff/context, delegation, todos and now Plan exit.

## Plan
**Goal:** Give managed Codex and Claude guests the same Relay capabilities as a native agent of the same pane/console scope, including Board creation and conditional tools.

**Findings:** `guest_board_bridge.py` filters native catalogs through a narrow allowlist. Native policy already lives in `Agent._prepare`/`_execute`, `BoardTools`, `AppTools`, and the executor. Guest-local shell, questions, and skills have harness equivalents; Relay UI/session/Board operations require bridging.

**Steps:**
1. Expand guest discovery from the live native catalogs, including Board creation/claim/tests/signals/try, app, session, keybinding, and handed-over program tools, while preserving scope and availability.
2. Dispatch every call through the existing Agent preparation and execution policy. Handle long test runs and per-turn program control without holding the MCP transport; retain explicit SSH host checks.
3. Replace stale fallback wording and add a catalog parity test covering native-only capabilities and guest equivalents.
4. Run targeted bridge/harness tests, then exercise real Codex and Claude discovery and representative success/refusal calls in disposable panes/boards. Record evidence.

**Risks:** Board imports still require owner authorization; cross-pane app writes use the existing toggle and target checks; program typing requires an active handover. Guest-local tools have different names and should be compared by capability.

**Verify:** Offline parity and policy tests, both real guest harnesses, and visible Relay events/audit records.

## Decisions
Owner, 2026-09-23: “guests should be able to create cards. build tool parity now. once its fully done, check it works. i want parity for the other differences you mentioend as well.” Guest card creation is authorized; pursue native capability parity, including the other interface differences in the audit.

## Execution Summary
The guest MCP bridge now derives its Board, app, session and conditional tool schemas from the live native catalogs, including `board_create_card`, `board_claim`, Board tests/signals, app actions and options, `session_info`, `activity`, `set_keybinding` and handed-over `type_into_program`. Dispatch uses `Agent._prepare` and `_execute`, preserving the native scope, write-toggle, read-only and grant checks. Foreground/background delegation and the 1–1800 second `agent_wait` range now match the native manager; Stop interrupts waits. Codex receives a process-local long MCP timeout for native test and foreground child calls. Guest-owned local shell, questions and skill reading remain capability equivalents.

## Tests
`PYTHONPATH=backend:tests python3 -m unittest tests.test_guest_board_bridge tests.test_guest_delegation tests.test_guest_memory tests.test_guest_harness_codex tests.test_guest_harness_claude tests.test_guest_harness_provider tests.test_app_tools tests.test_keybindings tests.test_board_tools tests.test_board.PolicyFileTests -q` — 668 passed. Real installed Codex and Claude Code managed-client turns each discovered `relay_board`, created and read a disposable card, and in a second turn called app/session tools, created and claimed a card, and checked its named tests. Details and limits: `docs/qa_evidence/2026-09-23-GPA8-guest-tool-parity/verification.md`. A wider optional run had four failures confined to concurrently changing Plan-mode code/tests, outside #GPA8.
