<!-- relay:entry 20260924T034424Z-ck author=agent kind=event model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-23-test-guest-harness-provider-catalogue-test-fails.md
- id: 20260924T045600Z-jxft
  ts: '2026-09-24T04:56:00Z'
  kind: evidence
  by: bd9e4ae0
  body: |
    Same failure, another combination and more timing, measured while delivering #Q8TM. On a
    clean export of main WITH that card's five files (commit 11847c), running
    `tests.test_guest_harness_provider tests.test_model_switch` (no test_guest_harness_codex
    involved) failed 2 of 7 runs, both in test_the_first_presets_answer_does_not_wait_for_codex:
    once rows["guest:codex"]["models"] non-empty on the first answer, once
    ['gpt-6-sol', 'gpt-5.6-terra', 'gpt-6-astra', 'gpt-6-luna'] where ['gpt-6-astra'] was
    expected — real `codex debug models` output reaching a test that mocks _read_codex_catalog,
    i.e. a real background scan landing in the module-global catalogue state. Plain HEAD: 0
    failures in 6 runs of the same pair; the single test 8/8 alone on both trees. So the race is
    real, timing-sensitive, and my switch-path change shifts it into reach for this combination
    too — consistent with this card's "module-level state leaking across tests" read.
