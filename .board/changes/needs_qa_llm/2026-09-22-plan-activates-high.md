---
id: PH9G
type: work
status: needs-qa-llm
labels: [bug, models, agent]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mph9g
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [1b05786b10754c5ffc81b8f7bcc47718b8965639], evidence: [docs/qa_evidence/2026-09-22-plan-high/README.md, docs/qa_evidence/2026-09-25-verify-PH9G/], related: [PMX7], github: null}
---
# Entering Plan activates High

## Issue
bug: going into plan mode isnt activating /high

## Done means
- Entering Plan selects High through the same role-selection path as /high.
- An already-High pane stays High; leaving Plan does not toggle High off.
- Lazy startup remembers High before the first prompt; configured panes request High before Plan.

## Plan
**Goal:** Activate High when entering Plan.
**Findings:** `Pane::setAgentMode` sends only set_mode; /high uses setAgentRole.
**Steps:** Reuse that selection path for configured and lazy panes, then exercise real Pane instances.
**Risks:** Preserve remembered High picks and avoid toggling an already-High pane back to Main.
**Verify:** Build Relay and run consolemode under Xvfb with isolated settings.
The default planning resolver must use the active model after a High switch, while preserving the saved Main role for /main. Add a recording-provider regression that selects High, runs a plan prompt and checks the served model and saved Main.

## Execution Summary
Entering Plan selects High through the /high path, including pending configuration. Default plan routing now follows the active High model while retaining the saved Main role. Evidence: docs/qa_evidence/2026-09-22-plan-high/README.md

## Tests
- `ctest --test-dir build -R '^consolemode$' --output-on-failure` — passed under Xvfb with isolated XDG_CONFIG_HOME.
- `PYTHONPATH=backend python3 -m unittest tests.test_plan_turns tests.test_roles` — 112 passed.
- manual: docs/qa_evidence/2026-09-22-plan-high/README.md

## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8` (clean worktree). Evidence: `docs/qa_evidence/2026-09-25-verify-PH9G/`.

**Done means, item by item:**
- Entering Plan selects High through the same path as /high — **passed by test evidence** (`enteringPlanSelectsHigh` green at HEAD via `--plan-click-only`).
- Already-High stays High; leaving Plan doesn't toggle it off — **passed by test evidence** (`planWhileConfiguringSelectsHighBeforeMode`, `clickingPlanLeavesModeAndPreservesDraft` green).
- Lazy startup remembers High; configured panes request High before Plan — **passed by test evidence** (`tests.test_plan_turns` + `tests.test_roles` OK at HEAD).

**Tests, line by line:**
- `ctest -R '^consolemode$'` — **failed at HEAD, not this card's fault**: 5 failures in editor/tool-call cases; checkout-verified fully green at this card's own commit 1b05786b; the drift is later work (#2M26, #PBZ4), filed #Y2PQ. This card's plan cases pass at HEAD.
- `PYTHONPATH=backend python3 -m unittest tests.test_plan_turns tests.test_roles` — **passed** (OK).
- manual README — **present**.

Unresolved: nothing for this card; the consolemode drift is #Y2PQ's.

Reviewed 2026-09-25 by the verifying session (qa-verify-PH9G), rev `2db96643`.
