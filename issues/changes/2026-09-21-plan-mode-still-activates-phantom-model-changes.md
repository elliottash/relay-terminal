---
id: PMX7
type: work
status: needs-verification
labels: [bug, models, agent]
assignee: codex
implemented_by: openai/gpt-5.6-sol via codex
rank: m7
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-phantom-plan-model-PMX7/README.md], related: [HR5E], github: null}
---
# Plan mode still activates phantom model changes

## Issue
there is still a weird bug where planning activates phantom model changes:

19a7fd1b741f433ab903e0aea3b65703

---
✦ plan that, as it affects a lot of things, as it
should be consistnet across the terminal and helper
agents, should work for agents and shell commands,
etc.
◆ Plan mode · this turn runs on glm-5.3 (z.ai ·
glm-5.3 · coding plan), then back to gpt-5.6-sol.

▸ gpt-5.6-sol
I’m using the project’s deliver workflow because
this change spans shared terminal and helper-agent
behavior. I’ll first map the board requirements and
---


## Done means
- Upgrading Relay retires the pre-#HR5E global `roles/planning/*` override once, so an existing Codex pane cannot silently route a plan turn to GLM.
- After that migration, an explicit model override chosen in the current Jobs UI remains supported and is not erased on later launches.
- Terminal panes and helper/console agents receive the same migrated role settings; ordinary shell commands never enter the planning route.
- Regression tests fail if the legacy override survives, if the migration repeats, or if the plan-mode UI still describes the old High-list behavior.

## Plan
**Goal**

Make #HR5E effective for upgraded installations: plan mode stays on the pane’s model by default instead of honoring a stale global planner pin.

**Findings**

- `/home/elliott/.local/share/relay/logs/worker.log`: session `19a7…` logged `plan_route` from `gpt-5.6-sol` to `glm-5.3`, `source=configured`.
- `/home/elliott/.config/RelayTerminal/relay.conf`: the configuring value is `roles/planning/preset=glm-coding` plus `effort=max`.
- `backend/relay_core/roles.py`: #HR5E fixed the default, but intentionally still honors configured planning entries.
- `src/main.cpp`, `src/JobsTab.cpp`: there is no migration for the now-obsolete pre-#HR5E planning override, and the Jobs description still says High may choose the planner.

**Steps**

1. Add a one-shot settings migration that removes existing `roles/planning/{tier,preset,model,effort}` values and marks the migration complete before any worker configuration is built.
2. Keep current/future explicit overrides intact after the marker, and update the Jobs copy to describe the actual default and override boundary.
3. Add isolated settings tests for legacy cleanup, fresh installs, and preservation after migration; add/update source agreement coverage if needed.
4. Run the focused GUI settings test plus backend plan-role/plan-turn tests, then land the exact changed tree.

**Risks**

- A user who intentionally set a planner before #HR5E loses that old pin once. This is necessary because old settings are indistinguishable from the stale global value causing the phantom route; future explicit choices remain durable.
- Startup ordering is load-bearing: the migration must run before panes or helper workers serialize `rolesObject()`.

**Verify**

- `jobstab_test` proves one-shot cleanup and future override preservation.
- `tests.test_roles` and `tests.test_plan_turns` prove the migrated empty role stays on the pane model at maximum supported effort, including guest harness panes.
- `scripts/relay-build --target jobstab-test` verifies the C++ integration against the exact shared checkout.

## Tasks

- [x] Trace the reported session and identify the persisted route source <!-- t:b6 -->
- [x] Retire legacy planning overrides once at the shared role-settings boundary <!-- t:mj -->
- [x] Preserve current explicit overrides and correct the Jobs description <!-- t:qn -->
- [x] Build and run focused GUI and backend plan-routing tests <!-- t:bq -->

## Execution Summary
The reported route was not a spontaneous provider change: the worker received an old global `roles.planning` pin to `glm-coding`, so `source=configured` correctly overrode #HR5E’s new own-model default. `rolestore::migrateLegacyPlanningOverride` now removes pre-fix planning role keys exactly once before `Pane::rolesObject` can configure either a terminal pane or a console/helper worker. The migration immediately marks fresh/upgraded settings, so any planner chosen afterward in the Jobs UI remains an explicit durable override. The Jobs copy and protocol now describe that boundary. Reproduction and verification evidence: `docs/qa_evidence/2026-09-21-phantom-plan-model-PMX7/README.md`.

## Tests
- `scripts/relay-build --target relay-jobstab-tests` — passed.
- `ctest --test-dir build -R '^jobstab$' --output-on-failure` — 1/1 passed.
- `PYTHONPATH=backend python3 -m unittest tests.test_roles tests.test_plan_turns` — 110 passed.
- `scripts/relay-build --target relay` — passed.
- `python3 scripts/relay-board.py check` — #PMX7 is structurally valid; the repository-wide check remains nonzero on pre-existing foreign card/thread errors (`python314-profile`, `LSP1`, `A9QR`, and others), none in #PMX7.
