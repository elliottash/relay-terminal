---
id: GPA8
type: work
status: planned
labels: [feature, guest, agent]
assignee: codex
rank: m
created: '2026-09-23'
source: 'Codex in a Relay pane, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [XP7N, 4NXH, AG7R], github: null}
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
**Goal:** Make guests able to use Relay pane capabilities without bypassing their existing policy checks.

**Steps:**
1. Add a catalog comparison test that classifies native tools as bridged, guest-native equivalent or explicitly excluded.
2. Bridge app and session tools through `Agent._prepare` / `_execute`, preserving app write gates, target-pane checks, and UI events.
3. Bridge the missing Board tools except `board_create_card` until its prior exclusion is reconsidered; preserve Board claim, test and signal gates.
4. Review conditional keybinding and program-control tools against guest-native behavior, then implement any missing capability with per-turn authorization.
5. Exercise Codex and Claude harness discovery and calls in live panes, including refusal paths.

**Risks:** #4NXH's five-tool decision was intentional; broadening Board writes needs to preserve ownership and rate limits. App commands can affect other panes, so keep the same target and write gates as the native agent.

**Verify:** Compare tool inventories in an automated test, run bridge policy tests, and confirm discovery and UI events in both guest harnesses.
