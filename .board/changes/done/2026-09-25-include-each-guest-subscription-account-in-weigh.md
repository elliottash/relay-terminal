---
id: F0AZ
type: work
status: done
labels: [bug, models, routing]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-sol via codex
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [9e0d6ed4cfd1], evidence: [], related: [RND7], github: null}
---
# Include every subscription account in weighted draws

## Issue
Weighted model selection currently uses only the account presets explicitly saved in priority lists. A newly registered Codex or Claude subscription account remains outside the draw even when its family model is tied at the highest rank.

> show me all accounts, their effective availability. and pick prob.
>
> the randomization is supposed to include all accounts independently, so codex can enter twice if there are two accounts for example.
> — elliott · [session:6a5b30a50b8f4918a5c207e1711a4232](relay://session/6a5b30a50b8f4918a5c207e1711a4232) · 2026-09-25

## Done means
A default Codex, Claude, Z.AI Coding Plan, or Kimi Code entry in a priority list contributes one candidate for each registered account serving that model. Each candidate uses its own quota report and can be selected independently; an explicitly ranked account is not duplicated. Logged-out guests and keyless plan accounts are ineligible, and unrelated model ranks stay unchanged.

## Plan
**Goal:** include each registered guest account as a separate weighted candidate whenever a saved list names its default guest model.

**Findings:** Qt draws new panes in `src/ModelCatalog.cpp`; workers draw job and fallback lists in `backend/relay_core/roles.py`. Both currently use only saved preset IDs. The worker catalog already gives each registered account its own preset and quota state.

**Steps:** expand default guest entries into matching account entries at the same rank in both draw paths; exclude known logged-out accounts; avoid duplicating explicitly listed account entries; test distinct quota weights and selection.

**Risks:** existing list order and explicit account overrides must remain stable; unknown login status at startup should remain provisional until the scan finishes.

**Verify:** targeted ModelCatalog C++ and Python role tests, then calculate the live odds from registered accounts and latest quota reports.

## Tests
- `ctest -R modelcatalog` — tests/modelcatalog_test.cpp; 75 passed, including independent guest and keyed plan accounts.
- `tests/test_roles.py::TiedRankTests::test_each_guest_account_has_its_own_weight_and_explicit_rank_wins` — passed.
- `tests/test_roles.py::TiedRankTests::test_keyed_plan_accounts_join_the_family_draw` — passed.
- Full `PYTHONPATH=backend python3 -m unittest tests.test_roles` — 83 passed.
- Exact-tree `land.py try` ModelCatalog build and test — passed.

### Check
Pass: account-specific weights, explicit-rank deduplication, logged-out guest exclusion, and keyed-plan account expansion.

## Execution Summary
Each saved family model now expands to eligible registered Codex, Claude, Z.AI Coding Plan, and Kimi Code accounts at the same rank. Qt new-pane draws and worker job/failover draws use each account's own quota weight. Explicit account entries retain their chosen rank; logged-out guests and keyless keyed accounts are excluded. Commit: `9e0d6ed4cfd1`.
