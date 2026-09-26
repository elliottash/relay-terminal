<!-- relay:entry 20260924T212035Z-xw author=agent kind=event model=glm-5.3 pane=13d4b665 turn=fc6d7f0a8d0c4f09aae22aaaf598ce32/e20d5f05a65443e6aec35a7a3ace5ad5 -->
- ✦ agent created this card in Planned · .board/changes/2026-09-24-word-wrap-lost-when-a-pane-is-restored-save-pros.md

<!-- relay:entry 20260924T212039Z-1a author=agent kind=event model=glm-5.3 pane=13d4b665 turn=fc6d7f0a8d0c4f09aae22aaaf598ce32/e20d5f05a65443e6aec35a7a3ace5ad5 -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by glm/glm-5.3, verify defaulted from skill deliver, session 13d4b665

<!-- relay:entry 20260924T212039Z-34 author=agent kind=progress model=glm-5.3 pane=13d4b665 turn=fc6d7f0a8d0c4f09aae22aaaf598ce32/e20d5f05a65443e6aec35a7a3ace5ad5 pane_token=13d4b665-1022-4d5a-90bc-7ec293856c1d -->
Claimed (13d4b665) · working on it from a terminal pane

Found the cause: the scrollback saver (AnsiSerializer) strips the OSC 8 `relay://prose/` runs, and the prose blocks' logical lines live only in the live view's fold map, so a restored pane replays hard-wrapped rows that never re-wrap. Plan: persist the logical lines as a trailer beside the rows, keep prose links through save+replay, re-register blocks on replay.

<!-- relay:entry 20260924T212045Z-f9 author=agent kind=event model=glm-5.3 pane=13d4b665 turn=fc6d7f0a8d0c4f09aae22aaaf598ce32/e20d5f05a65443e6aec35a7a3ace5ad5 -->
- ✦ agent updated this card · verify: {"artifact": "code", "primary": "script", "also": [], "human": "none", "sign_off… → {"artifact": "code", "primary": "script", "also": ["probe"], "human": "none", "s…; appended to `## Done means`

<!-- relay:entry 20260924T212052Z-f2 author=agent kind=event model=glm-5.3 pane=13d4b665 turn=fc6d7f0a8d0c4f09aae22aaaf598ce32/e20d5f05a65443e6aec35a7a3ace5ad5 -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260924T214459Z-eg author=agent kind=event model=glm-5.3 pane=13d4b665 turn=fc6d7f0a8d0c4f09aae22aaaf598ce32/e20d5f05a65443e6aec35a7a3ace5ad5 -->
- ✦ agent updated this card · appended to `## Execution Summary`; tasks: 5/5 done

<!-- relay:entry 20260924T214504Z-py author=agent kind=event model=glm-5.3 pane=13d4b665 turn=fc6d7f0a8d0c4f09aae22aaaf598ce32/e20d5f05a65443e6aec35a7a3ace5ad5 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260924T214507Z-9c author=agent kind=event model=glm-5.3 pane=13d4b665 turn=fc6d7f0a8d0c4f09aae22aaaf598ce32/e20d5f05a65443e6aec35a7a3ace5ad5 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented and landed in 1d80a83b with serializer, view and windowstate tests green; waits for verification. · evidence docs/qa_evidence/2026-09-24-prose-wrap-restore-MTCS/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260924T214509Z-4b author=agent kind=progress model=glm-5.3 pane=13d4b665 turn=fc6d7f0a8d0c4f09aae22aaaf598ce32/e20d5f05a65443e6aec35a7a3ace5ad5 -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260924T220258Z-0p author=agent kind=event model=glm-5.3 pane=a519a27c turn=fc6d7f0a8d0c4f09aae22aaaf598ce32/48b2a529ec614ad2a13296566405e8bb -->
- ✦ agent updated this card · appended to `## Try it`

<!-- relay:entry 20260925T180514Z-q8 author=agent kind=evidence -->
Check · 1 missing-evidence; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260925T181015Z-wx author=agent kind=evidence -->
Check · 1 missing-evidence; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.
