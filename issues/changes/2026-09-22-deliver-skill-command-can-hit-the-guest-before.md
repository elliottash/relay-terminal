---
id: D6VR
type: work
status: executing
labels: [bug, skills, guest]
assignee: codex
rank: m
created: '2026-09-22'
source: 'Codex in a Relay pane, 2026-09-22'
links: {plans: [], commits: [], evidence: [], related: [62M4], github: null}
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
3. Add and run a focused console-mode regression test.

**Risks:** Preserve built-in Relay commands and guest slash commands after the catalog arrives.

**Verify:** Build and run `relay-consolemode-tests` through the documented build wrapper.
