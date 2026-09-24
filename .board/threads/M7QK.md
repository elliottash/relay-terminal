- id: 20260924T045500Z-m7qk
  ts: '2026-09-24T04:55:00Z'
  kind: note
  by: bd9e4ae0
  body: |
    Filed while delivering #Q8TM (measured on a clean export of main at 79dd56a, before that
    card's files were applied — this is not caused by 11847c).

<!-- relay:entry 20260924T043132Z-z1 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T043132Z-z2 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260924T043220Z-za author=agent kind=event model=kimi-k3 pane=switchboard turn=f000ad02481ff249d8dfdf162788bae8/7dc7db52751d44ccac19906143793bf2 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T043247Z-6s author=agent kind=event model=kimi-k3 pane=switchboard turn=f000ad02481ff249d8dfdf162788bae8/7dc7db52751d44ccac19906143793bf2 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T043253Z-tx author=agent kind=comment mode=plan model=kimi-k3 turn=f000ad02481ff249d8dfdf162788bae8/7dc7db52751d44ccac19906143793bf2 -->
The literal still exists in the source, so the failure is likely about which file the test reads. Let me check the test.









**Planned:** the dedup wasn't dropped — it moved to `src/PaneRuntime.cpp` in the #243T split, guard literal intact. The plan is a one-test fix: point `test_the_backend_directory_is_appended_to_pythonpath_only_once` at `PaneRuntime.cpp` via a new path constant, keeping its assertions unchanged, then run the `MirroredInCxx` class plus a clean-export check. Nothing needed from you — it's ready to **Run**.

<!-- relay:entry 20260924T043253Z-ty author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
