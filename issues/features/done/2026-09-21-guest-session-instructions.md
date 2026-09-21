---
id: GP1N
type: work
status: done
labels: [feature, guests, agents]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex in Relay, 2026-09-21'
links: {plans: [], commits: [28e94fef3b14fc73e1196b310ce55cd64f69abd1], evidence: [docs/qa_evidence/2026-09-21-guest-instructions/README.md], related: [GT7X, 4NXH], github: null}
---
# Attach Relay instructions to guest sessions through their native interfaces

## Issue
ok do this:

Attach it when the guest session starts, through
  the guest’s instruction interface

its ok if this doesnt apply to existing sessions before we implement, but should apply to new sessions and then those new sessions when they are further resumed. 

yes to the guest specific version that has the relevant claridication. 

i dont necessarily want to explicitly encourage delegation, tell me how the claude system prompt does it and see if there are other sources online with advice on how to frame it, where its used when warranted.

## Plan
**Goal:** New Claude/Codex guest sessions receive Relay-specific instructions outside user messages, retained on subsequent resumes.

**Findings:** `guest_harness_provider.start_provider` starts both adapters without instructions. Claude supports `--append-system-prompt`; Codex start/resume/fork supports `developerInstructions`. Claude keeps a session prompt snapshot and relaunches for effort changes.

**Steps:**
1. Define concise guest instructions covering Relay context, actual tool availability, project policy, and evidence-based reporting without proactive delegation language.
2. Add an optional instructions argument to the harness contract and pass it from the provider on start/resume/fork; retain it through Claude relaunch.
3. Test startup transport, resume/fork, relaunch, and unchanged user messages. Document the protocol and delegation research.

**Risks:** Do not replace guest defaults or advertise native Relay tools that are unavailable. Existing Claude prompt snapshots need no migration. Prompt transport does not guarantee an override of guest delegation policy.

**Verify:** Targeted guest harness/provider unit tests and installed CLI/schema inspection; no paid model turns.

## Execution Summary
Added a guest-specific Relay supplement through Claude's append-system-prompt flag and Codex's developerInstructions field. Supplied on start/resume/fork, retained on Claude relaunch, never inserted as user text. Guest defaults and delegation policy remain intact. Recorded installed Claude Agent-tool prompt variants and online delegation advice in `docs/qa_evidence/2026-09-21-guest-instructions/README.md`.

## Tests
`PYTHONPATH=backend:tests python3 -m unittest test_guest_harness_provider test_guest_harness_claude test_guest_harness_codex test_guest_board_bridge`

## Resolution
Completed with 221 targeted tests passing and installed CLI/schema inspection. No paid model turns; verification covers transport and lifecycle, not model obedience. Older guest sessions require no migration.
