<!-- relay:entry 20260924T235548Z-w4 author=agent kind=event model=glm-5.3 pane=1e29c6d7 turn=37bd69d174a84a0bac5c8b54fe01f282/4c7a2e70a762441bbfd894c43b1f848b -->
- ✦ agent created this card in Planned · .board/changes/2026-09-24-esc-cannot-clear-a-pane-wedged-busy-by-a-mid-tur.md

<!-- relay:entry 20260924T235552Z-nm author=agent kind=event model=glm-5.3 pane=1e29c6d7 turn=37bd69d174a84a0bac5c8b54fe01f282/4c7a2e70a762441bbfd894c43b1f848b -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by glm/glm-5.3, session 1e29c6d7

<!-- relay:entry 20260924T235552Z-yc author=agent kind=progress model=glm-5.3 pane=1e29c6d7 turn=37bd69d174a84a0bac5c8b54fe01f282/4c7a2e70a762441bbfd894c43b1f848b pane_token=1e29c6d7-27b1-4017-9d49-c073e0f3b89c -->
Claimed (1e29c6d7) · working on it from a terminal pane

Implementing the two fixes: queue.py exclusive tasks emit queue_changed at start/end; PaneEvents.cpp clears a stale busy flag when queue_changed reports nothing running. Tests in test_queue.py and a pane-state test.

<!-- relay:entry 20260925T000627Z-9q author=agent kind=event model=glm-5.3 pane=1e29c6d7 turn=37bd69d174a84a0bac5c8b54fe01f282/4c7a2e70a762441bbfd894c43b1f848b -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "none", "criteria…; appended to `## Done means`

<!-- relay:entry 20260925T000632Z-db author=agent kind=event model=glm-5.3 pane=1e29c6d7 turn=37bd69d174a84a0bac5c8b54fe01f282/4c7a2e70a762441bbfd894c43b1f848b -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260925T000639Z-jd author=agent kind=event model=glm-5.3 pane=1e29c6d7 turn=37bd69d174a84a0bac5c8b54fe01f282/4c7a2e70a762441bbfd894c43b1f848b -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260925T000643Z-nc author=agent kind=event model=glm-5.3 pane=1e29c6d7 turn=37bd69d174a84a0bac5c8b54fe01f282/4c7a2e70a762441bbfd894c43b1f848b -->
- ✦ agent moved this card · Executing → Needs verification · Both fixes landed (63d77a72, e3772311) with tests and verify builds; waits for verification per qa_policy. · implemented_by glm/glm-5.3
