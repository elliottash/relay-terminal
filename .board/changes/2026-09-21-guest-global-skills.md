---
id: GSK7
type: work
status: needs-verification
labels: [bug, guest, skills]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: Owner via Codex in Relay, 2026-09-21
links: {plans: [], commits: [778b6d68d26a884fbc2e830f099463c17fa46fc2], evidence: [tests/test_guest_harness_provider.py], related: [B2XF], github: null}
---
# Global skills reach guest harnesses

## Issue
there was a bug with codex (maybe all guest harnesses) where it couldnt see my global skills, which the non-guests could see

## Plan
Use Relay's configured skill index in the shared guest instruction builder, listing names, trigger descriptions and absolute SKILL.md paths. Both adapters already preserve supplemental instructions on start/resume/fork. Respect disabled skills and exclusions. Verify both guests with fake harnesses and temporary global skill directories, plus the existing skill and adapter tests.

## Execution Summary
Both Codex and Claude receive Relay's configured skill catalog as supplemental instructions, including trigger descriptions and absolute SKILL.md paths. Model switches and planning guests reuse the pane's skill index, preserving disabled skills and exclusions. Catalog entries supplement harness-native discovery; full skill bodies are loaded on demand.

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_provider tests.test_skills tests.test_guest_harness_codex tests.test_guest_harness_claude tests.test_session_protocol`

273 tests passed on a clean HEAD export with this change applied. Covers both guests, global Warp and nested Claude skills, start/resume/fork, configured directories, disabled skills, exclusions and reuse of the pane's index. No paid live guest turns run. Board check has pre-existing errors and warnings elsewhere; none names GSK7.

## QA checklist
- Start a fresh Codex and Claude guest using the updated backend; ask each to find and read an existing global Warp skill by name.
- Confirm an excluded skill is absent from the Relay catalog.
- Switch a native pane with custom skill directories to a guest and confirm those skills remain available.
