---
id: JXFT
type: work
status: inbox
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-23'
source: pane 2, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [V1VM], github: null}
---
# test_guest_harness_provider catalogue test fails when run after the codex harness tests

## Issue
Unrelated fault noticed while landing #V1VM: tests.test_guest_harness_provider.CatalogueTests.test_the_first_presets_answer_does_not_wait_for_codex fails when the module runs together with tests.test_guest_harness_codex (both from tests/), but passes alone and in smaller combinations. Measured on the committed tree with no local changes: `git archive HEAD backend tests | tar -x -C /tmp/pristine && PYTHONPATH=/tmp/pristine/backend python3 -m unittest tests.test_guest_harness_provider tests.test_guest_harness_codex` → FAILED (failures=1) on 2026-09-24, main at ca195bfa: rows["guest:codex"]["models"] is non-empty where the test expects []. Looks like module-level state leaking across test modules (a cached installation/adapter or fake harness registry), i.e. test-order dependence, not a product fault.
