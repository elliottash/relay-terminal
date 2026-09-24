---
id: RND7
type: work
status: needs-verification
labels: [feature, models, routing, ui]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Owner in a Relay guest session, 2026-09-23
links: {plans: [], commits: [2f8ee33de91181bd4bbfff10eda22041ffce7a84, d26386dcdccc42124f688dd46ec6663e31b3c246, 37d3cdb33ab29f43b234dc9456f8a0984968ec9b, 6063dff7c9148029c1736201b1c3a9300b561457, 1342a328f8464551ad29b46ebf44d5ac504e41ab, 4d52159e97acf93bb4688f7222f824112a9fc4c4, 1c44effe85a7d5ffb1389ee85f79eae04d3ec8f1, d86b205a8960210b9c09f10cf37dcd890fd92f6a], evidence: [docs/qa_evidence/2026-09-23-rnd7-tied-models/01-priorities.png, docs/qa_evidence/2026-09-23-rnd7-tied-models/02-jobs.png, docs/qa_evidence/2026-09-23-rnd7-tied-models/], related: [XH4K], github: null}
---
# Tied model ranks and subscription-aware selection

## Issue
analyze opportunities in the model pickers to add randomness. allow an option for models to be given the same rank, and then relay will randomize across them.

so for example, i assign fable 5.1 high or astra xhigh for planning; when i do shift tab to plan, one is picked randomly.

ditto for subagents, main, helper agent, etc

another nice feature we can implement here is that, we can do sorting, or weighted randomization, to optimize usage of subscription plans. so if i have a lot left on a subscription thats about to reset, relay should use that one (or more likely to use it)

## Decisions
- “Allow a separate ranked list per job, with shared lists as defaults (recommended)” and “yes” to drawing when entering Plan mode, starting a new main/helper conversation, or spawning each subagent, then keeping it for follow-ups.
- Working assumption pending correction: subscription usage weights models within the highest eligible rank; explicit ranks remain hard priorities.

## Done means
Equal-rank model entries can be configured for main, planning, helper, and subagents, with their model and effort paired. A fresh lifecycle draw chooses among eligible entries in the best available rank and follows the chosen model for later turns.
Known, recent subscription usage can favor unused allowance approaching reset without crossing rank boundaries. Missing usage data yields equal odds, and exhausted models are skipped. Legacy lists keep their current ordering.

## Plan
**Goal:** introduce tied ranks and optional job-specific ranked lists, with subscription-aware weighting at the agreed lifecycle boundaries.

**Findings:** the tier list lives in `src/ModelCatalog.h` and `src/ModelCatalog.cpp`; the picker and Jobs tab are `src/ModelPicker.cpp` and `src/JobsTab.cpp`; pane startup is `src/Pane.h`, helper startup `src/RelayWindow.h`, and worker routing is `backend/relay_core/roles.py`, `agent.py`, and `subagents.py`. Existing usage limits carry percentages and reset times; the current “subscription left” sort ignores resets. GLM/Kimi polling is tracked separately in #XH4K.

**Steps:**
1. Store an explicit rank on each tier entry, migrating old lists to distinct ranks. Expose tied ranks in the picker without disturbing ongoing layout edits.
2. Add optional ranked model lists to the Jobs tab for planning, helper, and subagents, following their shared tier until customized.
3. Draw once at each lifecycle boundary, preserve the concrete selection for follow-ups and restoration, and try other tied peers before lower ranks on failure.
4. Weight a tied draw toward remaining allowance approaching reset when recent limits are available. Keep unknown limits neutral and exhausted entries out.
5. Add focused storage, selection, fallback, and UI tests, then capture an isolated screenshot of the picker and Jobs tab.

**Risks:** startup and status refreshes must not cause redraws; effort must travel with its model; shared checkout edits in the picker, Jobs tab, Pane, and RelayWindow require careful coordination.

**Verify:** targeted `modelcatalog`, `modelpicker`, `jobstab` C++ tests and Python role/agent/subagent tests; isolated Xvfb capture of tied ranks and job overrides.

## Execution Summary
Implemented tied ranks for shared model lists and optional Planning, Subagents, and Helper lists. Selection draws among eligible models at the best rank, weights peers when recent quota and reset data are known, preserves the selected model for follow-ups, and tries tied peers before lower ranks on failover. The picker displays equal ranks and the Jobs tab shows job-specific random choices.

## Tests
- Exact-tree `relay` build passed for each RND7 C++ landing commit.
- `PYTHONPATH=backend python3 -m unittest tests.test_roles tests.test_subagents`: 104 passed.
- `build/relay-modelcatalog-tests -silent`: 70 passed.
- `xvfb-run -a build/relay-jobstab-tests -silent`: 23 passed.
- `xvfb-run -a build/relay-modelspane-tests tiedRanksAndPlanningListAreVisible -silent`: 3 passed; screenshots at `docs/qa_evidence/2026-09-23-rnd7-tied-models/`.
- The full ModelPicker test binary has an unrelated existing footer wording mismatch from concurrent narrow-pane edits; its RND7 rank changes compile in the exact-tree build.
