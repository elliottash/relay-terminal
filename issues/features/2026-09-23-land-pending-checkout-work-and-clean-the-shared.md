---
id: Z55B
type: work
status: executing
labels: [feature, repository]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 49dbf51d-820d-4c71-8c2c-f411cacebb78
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: Owner in Relay guest pane, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [TYH4, C7NQ, 5Z6N], github: null}
---
# Land pending checkout work and clean the shared tree

## Issue
bring it all in and do the cleanup

## Done means
Every existing tracked code, test, documentation, and board change is either landed in a reviewed commit or explicitly identified as work still being edited by another active pane. Untracked evidence is preserved in its related card or a safe location. `git status` is clean if no other pane writes during the final check; no credentials, transient logs, or user data are silently committed or deleted.

## Plan
**Goal.** Preserve and land the remaining work visible in the shared `main` checkout, then remove only confirmed disposable leftovers.

**Findings.** The tree changes concurrently. Pending code spans Sessions, middle-click tabs, file editor tests, and a new rebuild-speed refactor; board cards and evidence are also dirty. The owner inbox files are excluded by `land.py`.

**Steps.** (1) Snapshot the status and classify each path by owning card and purpose. (2) For each coherent group, claim its card if needed, review exact hunks, run targeted checks, and land with `scripts/land.py`. (3) Reconcile board card moves, thread updates, and evidence separately. (4) Inspect untracked logs/output for value or sensitive material; preserve or remove only after their disposition is clear. (5) Recheck status and report anything still actively edited.

**Risks.** Other panes are editing `main` concurrently, so a clean status may be transient. Builds must validate the exact committed tree; broad path commits can include incomplete work. Generated logs and `threads/` may hold user data.

**Verify.** Targeted subsystem tests, `land.py` exact-tree build gate for C++ groups, `git diff --check`, and final `git status --short`.
