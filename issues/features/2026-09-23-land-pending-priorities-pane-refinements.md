---
id: Y2B9
type: work
status: needs-verification
labels: [feature, models]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 49dbf51d-820d-4c71-8c2c-f411cacebb78
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Owner in Relay guest pane, 2026-09-23
links: {plans: [], commits: [], evidence: [tests/modelpicker_test.cpp], related: [RKP3, RND7], github: null}
---
# Land pending Priorities pane refinements

## Issue
claim all of those and merge them in

## Done means
The three pending ModelPicker files land on main with their intended Priorities layout and hint behavior. The picker builds and its targeted tests pass; no unrelated shared-checkout changes enter the commit.

## Plan
**Goal.** Land the existing Priorities pane refinements already in the shared checkout.

**Findings.** Uncommitted edits are limited to `src/ModelPicker.cpp`, `src/ModelPicker.h`, and `tests/modelpicker_test.cpp`: quota status moves into the via cell, out-of-box rows dim, no-knob reasoning rail hides, and a one-shot controls hint replaces permanent footer text.

**Steps.** Claim the three paths from HEAD, review their exact diff, build the picker test target, run the focused picker tests, fix any regression in these paths, and commit only the reviewed hunks through `scripts/land.py`.

**Risks.** Shared-checkout sessions have old claims on picker files; preserve concurrent edits and verify the commit's path set. The one-shot hint depends on event-loop timing.

**Verify.** `git diff --check`, target build via `scripts/relay-build`, and `relay-modelpicker-tests` for the changed behavior.

## Execution Summary
Claimed the existing three-file Priorities picker diff from HEAD. It folds exhausted status into the provider cell, dims rows past the model-box cutoff, hides the unused reasoning rail on sectioned pages, and shows a one-time controls hint. Fixed the permanent footer to omit move instructions as the new test requires. Reviewed the scoped diff and left unrelated shared-checkout edits untouched.

## Tests
- `git diff --check -- src/ModelPicker.cpp src/ModelPicker.h tests/modelpicker_test.cpp` — passed.
- `scripts/relay-build --target relay-modelpicker-tests` — passed.
- `QT_QPA_PLATFORM=offscreen build/relay-modelpicker-tests -silent` — 55 passed.
- `scripts/relay-build --target relay-modelspane-tests` — passed.
- `QT_QPA_PLATFORM=offscreen build/relay-modelspane-tests -silent` — 24 passed.
