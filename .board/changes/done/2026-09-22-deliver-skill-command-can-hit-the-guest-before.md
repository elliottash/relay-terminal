---
id: D6VR
type: work
status: done
labels: [bug, skills, guest]
assignee: codex
implemented_by: openai/gpt-5.6-sol via codex
verified_by: openai/gpt-5.6-sol via codex
rank: m
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [97b96861635151be9ee1533159a052f618431a8f, e8709709e94945fdedd13f395fae439d14730024], evidence: [], related: [62M4], github: null}
---
# /deliver can reach the guest before Relay loads its skill catalog

## Issue
bug: this threw an error:

/deliver #62M4

## Done means
- A skill slash command entered while a deferred guest pane is starting waits for Relay's skill catalog, then runs as an agent prompt with that skill attached.
- A slash command that is still not a Relay skill after configuration retains the existing guest/unknown-command behavior.
- A regression test covers the pre-configuration command path.

## Plan
**Goal:** Remove the startup race that sends `/deliver` to the guest CLI before Relay knows it is a skill.

**Findings:** `src/Pane.h` checks `m_skillCommands` before a deferred harness is configured. The live log for pane `0611198f` shows the command was entered before the `configured` event populated that catalog.

**Steps:**
1. Queue unresolved slash commands while configuration is pending.
2. Reclassify the queued command after `configured` supplies the skill catalog.
3. Add and run a focused slash-command regression test.

**Risks:** Preserve built-in Relay commands and guest slash commands after the catalog arrives.

**Verify:** Build `relay` through `scripts/relay-build`, then run `QT_QPA_PLATFORM=offscreen ./build/relay-slash-tests`.

## Tasks

- [x] Queue unresolved slash commands until configuration supplies the skill catalog <!-- t:r5 -->
- [x] Reclassify queued commands as Relay skills, guest commands, or unknown commands <!-- t:nc -->
- [x] Add a regression test and build the exact landed tree <!-- t:1t -->

## Execution Summary
Commit `97b96861635151be9ee1533159a052f618431a8f` queues name-shaped slash commands entered during deferred configuration, then resolves them after `configured.skill_commands` arrives. Relay skills start agent turns with their skill attachment; non-skills retain the guest or unknown-command path.

## Tests
- `QT_QPA_PLATFORM=offscreen ./build/relay-slash-tests` — 11 passed, including `/deliver #62M4` before catalog arrival.
- `scripts/relay-build --target relay` — passed.
- `scripts/land.py commit codex-deliver-startup …` exact-tree verification — passed.
