<!-- relay:entry 20260923T121734Z-a1 author=codex kind=progress -->
### Codex · 2026-09-23 12:17 UTC
Filed the close-time final recap decision separately from the Sessions table UI in #PV7W. The current automatic summary cadence can leave the saved recap older than the final turn.

<!-- relay:entry 20260923T121757Z-tn author=agent kind=question model=gpt-6-sol pane=d3adcb62 turn=f4520a5d8d7b4fed83890c56e50a4052/5eaf94c328b34ad9a15b0d908d04b473 -->
1. Should closing a session automatically generate a missing or stale final recap? Recommendation: yes, but run it after close in a separate worker so closing the pane stays immediate, and keep the existing saved recap if generation fails. The Sessions table change in #PV7W can land independently.

<!-- relay:entry 20260923T121757Z-to author=agent kind=event model=gpt-6-sol pane=d3adcb62 turn=f4520a5d8d7b4fed83890c56e50a4052/5eaf94c328b34ad9a15b0d908d04b473 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260923T121807Z-vs author=agent kind=event model=gpt-6-sol pane=d3adcb62 turn=f4520a5d8d7b4fed83890c56e50a4052/5eaf94c328b34ad9a15b0d908d04b473 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260923T122034Z-aj author=agent kind=event model=gpt-6-sol pane=d3adcb62 turn=f4520a5d8d7b4fed83890c56e50a4052/5eaf94c328b34ad9a15b0d908d04b473 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["PV7W"], "github": null… → {"plans": [], "commits": ["a90f5e423aef80a8a6a4e0f3ddf01b8193d1c2ae"], "evidence…

<!-- relay:entry 20260923T154656Z-c6 author=agent kind=decision model=gpt-6-sol pane=6b3079fc turn=f4520a5d8d7b4fed83890c56e50a4052/cca5ff3484aa434485bea234e7d8900e -->
The owner chose: “yes, generate on close.” Generate a missing or stale final recap automatically when a session closes.

<!-- relay:entry 20260923T154712Z-ns author=agent kind=event model=gpt-6-sol pane=6b3079fc turn=f4520a5d8d7b4fed83890c56e50a4052/cca5ff3484aa434485bea234e7d8900e -->
- ✦ agent updated this card · waiting_on: owner → (unset); replaced `## Decisions`

<!-- relay:entry 20260923T154715Z-c1 author=agent kind=event model=gpt-6-sol pane=6b3079fc turn=f4520a5d8d7b4fed83890c56e50a4052/cca5ff3484aa434485bea234e7d8900e -->
- ✦ agent moved this card · Discussing → Executing · Owner approved automatic final recap generation; implementing a background close path. · implemented_by openai/gpt-6-sol via codex

<!-- relay:entry 20260923T154903Z-sc author=agent kind=event model=gpt-6-sol pane=6b3079fc turn=f4520a5d8d7b4fed83890c56e50a4052/cca5ff3484aa434485bea234e7d8900e -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260923T154911Z-hg author=agent kind=event model=gpt-6-sol pane=6b3079fc turn=f4520a5d8d7b4fed83890c56e50a4052/cca5ff3484aa434485bea234e7d8900e -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T155637Z-77 author=agent kind=progress model=gpt-6-sol pane=6b3079fc turn=f4520a5d8d7b4fed83890c56e50a4052/cca5ff3484aa434485bea234e7d8900e -->
Implemented a detached final-summary helper on worker shutdown. It writes only metadata and the index; the saved session file remains owned by any pane that resumes. Added successful-summary turn tracking so a failed periodic refresh does not make an older recap look current. Focused tests, including a helper that finishes after its parent exits, pass.

<!-- relay:entry 20260923T155955Z-1y author=agent kind=event model=gpt-6-sol pane=6b3079fc turn=f4520a5d8d7b4fed83890c56e50a4052/cca5ff3484aa434485bea234e7d8900e -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["a90f5e423aef80a8a6a4e0f3ddf01b8193d1c2ae"], "evidence… → {"plans": [], "commits": ["a90f5e423aef80a8a6a4e0f3ddf01b8193d1c2ae"], "evidence…; appended to `## Execution Summary`

<!-- relay:entry 20260923T160001Z-aq author=agent kind=event model=gpt-6-sol pane=6b3079fc turn=f4520a5d8d7b4fed83890c56e50a4052/cca5ff3484aa434485bea234e7d8900e -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260923T160009Z-ec author=agent kind=event model=gpt-6-sol pane=6b3079fc turn=f4520a5d8d7b4fed83890c56e50a4052/cca5ff3484aa434485bea234e7d8900e -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T160018Z-n3 author=agent kind=event model=gpt-6-sol pane=6b3079fc turn=f4520a5d8d7b4fed83890c56e50a4052/cca5ff3484aa434485bea234e7d8900e -->
- ✦ agent moved this card · Executing → Needs verification · Detached close recap and persistence checks implemented; 258 targeted tests and the Relay build pass. · evidence docs/qa_evidence/2026-09-23-final-recap-close/ · implemented_by openai/gpt-6-sol via codex
