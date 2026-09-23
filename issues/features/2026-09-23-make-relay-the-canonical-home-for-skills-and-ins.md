---
id: HS7V
type: work
status: needs-verification
labels: [feature, skills, instructions]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: e0e12343-9768-4af9-a4ee-96b00ae19807
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [tests/test_skills.py], related: [XHXX, MEPR], github: null}
---
# Make .relay the canonical home for skills and instructions

## Issue
we shouldnt use .warp as the default place for skills or anything else. we should centralize in .relay and route to existing skill / instruction surfaces from there.

## Done means
- Project `.relay/skills` and `.relay/relay.md` are Relay's default project skill and instruction surfaces; duplicate skill names resolve to Relay-owned copies before external ones.
- Existing Warp, Claude, and Codex skill/instruction locations remain discoverable as external sources, and Relay instructions can import an existing project instruction file.
- Relay's docs and ecosystem strategy describe the Relay paths as canonical; focused tests prove precedence, fallback, and source labels.

## Plan
**Goal.** Make `.relay` the project namespace Relay reads first, while preserving access to other tools' files.

**Findings.** `skills.default_directories` starts with global Relay copies then Warp and Claude; it omits project `.relay/skills`. `instructions.PROJECT_ORDER` chooses `WARP.md` first; `@path` imports already let one chosen instruction file route to another. Relay's global owned locations already use XDG `~/.config/relay`.

**Steps.** 1. Put project `.relay/skills` ahead of external skill trees and keep the explicit `project` flag compatible. 2. Prefer `.relay/relay.md` for project instruction discovery and label it as Relay, retaining external candidates and imports. 3. Update docs and the plugin ecosystem report, then add focused precedence and fallback tests.

**Risks.** Changing default precedence may expose a previously shadowed project skill or instruction; list order and source labels must make that visible. Keep `WARP.md` as a fallback for older projects.

**Verify.** Run targeted skill and instruction tests, a Board check, and inspect the discovery order in the updated documentation.

## Decisions
- Owner: “we shouldnt use .warp as the default place for skills or anything else. we should centralize in .relay and route to existing skill / instruction surfaces from there.” Project `.relay/` is Relay's canonical namespace; existing tool files remain readable as sources.

## Execution Summary
Relay project skill discovery now starts with `.relay/skills`, followed by global Relay skills; shared, Claude, Codex and Warp locations are compatible sources with visible provenance labels. Project instruction discovery starts with `.relay/relay.md` and can `@`-import existing instructions; absent that file, `AGENTS.md` precedes `WARP.md`. The README, architecture and plugin ecosystem report describe these owned and external paths.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_skills tests.test_session_protocol.InstructionTests` — 34 passed.
- `PYTHONPATH=backend python3 -m unittest tests.test_routing_thinking_skills.RefineTests tests.test_board.PolicyFileTests` — 19 passed.
- `git diff --check` on the changed code, tests, README, architecture and report — passed.
- `python3 scripts/relay-board.py check` — 14 pre-existing errors in unrelated legacy cards/threads; no #HS7V diagnostic.
