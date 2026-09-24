---
id: NYXV
type: work
status: planned
labels: [bug, models, tests]
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# tests.test_tier_lists: relay-pro cloud rows fail test_every_cloud_row_carries_both_keys (3 subtests, pre-existing at HEAD)

## Issue
Found while delivering #Y4PJ: `PYTHONPATH=backend python3 -m unittest tests.test_tier_lists.StartEffortTests.test_every_cloud_row_carries_both_keys` fails 3× at HEAD (and unchanged after #D0MC/##Y4PJ) — subtests (preset='relay-pro', model='relay-pro-high' | 'relay-pro-main' | 'relay-pro-flash'): the relay-pro cloud rows do not carry both keys the test requires. Pre-existing on main; unrelated to the tier-defaults change (verified identical in a clean HEAD export in /tmp).

## Done means
`PYTHONPATH=backend python3 -m unittest tests.test_tier_lists` passes in full at HEAD — including `StartEffortTests.test_every_cloud_row_carries_both_keys` for the three relay-pro rows (relay-pro-high, relay-pro-main, relay-pro-flash). The test's invariant still holds in substance: every cloud row's stored `tier_effort` is exactly what `tier_start_efforts` computes from that row's own data. The relay-pro picker behaviour is unchanged: relay-pro rows still rank their start efforts by the upstream model name (`glm-5.3` / `glm-5.3-flash`), per `_ranking_name`'s contract. Failure looks like: the unittest still fails a relay-pro subtest, or the fix instead changes what `catalog_rows('relay-pro')` stores (a picker behaviour change smuggled in as a test fix).

## Plan
**Goal** — Make `tests.test_tier_lists.StartEffortTests.test_every_cloud_row_carries_both_keys` pass for the three relay-pro catalog rows by fixing the test's recomputation to use the same model name `catalog_rows` uses, without changing any picker behaviour.

**Findings** — The mismatch is a name, not a missing key. In `backend/relay_core/presets.py`:
- `catalog_rows()` (line ~729) stores each row's `tier_effort` as `tier_start_efforts(efforts, provider_default, "", _ranking_name(preset_id, row["id"]))` — for relay-pro rows `_ranking_name` returns the catalog row's upstream `name` (`glm-5.3`, `glm-5.3-flash`), which **is** in `model-ranking.md`'s Levels table.
- But the row's public `"name"` is `model_name(preset_id, id)` (line ~600), which for the hosted `relay-pro` preset deliberately returns the role name `"relay pro · high"` / `· main` / `· flash` — names that are **never** in the ranking file.
- The test (`tests/test_tier_lists.py` line ~681) recomputes `P.tier_start_efforts(row['efforts'], row['default_effort'], '', row['name'])`, i.e. with the role name. `tier_start_efforts` (line ~948) then finds no Levels row and returns the pure rule fallback, while the stored value used the glm-5.3 Levels (`high=max, main=high, flash=low`; glm-5.3-flash: `flash=high`) mapped through `nearest_effort` into relay-pro's `("low","medium","high")`. They diverge wherever a Levels cell differs from the rule (e.g. relay-pro-main: stored main `'high'` from the table vs recomputed `'medium'` from the provider default; relay-pro-flash: stored `main='high', flash='high'` vs `'low','low'`).
- The divergence is **intended behaviour on the catalog side**: `_ranking_name`'s docstring says "hosted public role names never change defaults", and `intelligence` uses the same ranking name. So the fix belongs in the test, not in `catalog_rows`.

**Steps**
1. Reproduce first: `PYTHONPATH=backend python3 -m unittest tests.test_tier_lists.StartEffortTests.test_every_cloud_row_carries_both_keys -v` and read the three subtest failure messages — confirm each is the equality assert and note the exact dicts (static analysis predicts relay-pro-main and relay-pro-flash diverge on `main`/`flash`; if relay-pro-high fails on a different assert or different values, record what the runner actually says before editing).
2. In `tests/test_tier_lists.py`, `test_every_cloud_row_carries_both_keys`: change the recomputation's name argument from `row['name']` to `P._ranking_name(preset_id, row['id'])` — the same expression `catalog_rows` uses — so the assert compares like with like. Add a one-line comment saying why: the stored value is computed from the ranking-table name, which for hosted relay-pro rows is the upstream model, not the public role name.
3. Nothing else changes: no edit to `presets.py`, `model_ranking.py` or `model-ranking.md`.

**Risks**
- If step 1 shows a relay-pro row failing the membership assert (`level in row['efforts']`) or the key-set assert instead of the equality, the diagnosis is wrong — stop and re-read `catalog_rows` before editing.
- Alternative "fix" to avoid: making `catalog_rows` store `tier_effort` from the public name would make the test pass too but silently changes what the picker starts relay-pro models at — a behaviour change this card does not ask for. If the runner believes the *catalog* is wrong (hand-added relay-pro rows go through the public name), that is a product question for the owner, not this card.

**Verify** — `PYTHONPATH=backend python3 -m unittest tests.test_tier_lists` passes in full (the whole file, not just the one test — the change touches shared test code). `git diff` shows only `tests/test_tier_lists.py`.
