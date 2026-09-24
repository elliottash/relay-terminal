<!-- relay:entry 20260919T153159Z-fx author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Ready → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260919T153159Z-fy author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260919T153214Z-0b author=agent kind=event model=glm-5.3 pane=b762ee0e turn=25c2c83c430b4960be9088b8492e2ee5/fbae4046686c43bb8db4b18f4840110c -->
- ✦ agent updated this card · implemented_by: (unset) → claude-opus-4-5

<!-- relay:entry 20260919T161814Z-hx author=agent kind=event model=glm-5.3 pane=f997f065 turn=25c2c83c430b4960be9088b8492e2ee5/fd2d0bde4aef48288e8114906071580d -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["4E13", "V8KT"], "githu… → {"commits": ["7a5221b"], "evidence": ["docs/qa_evidence/2026-09-19-relaying-line…; tasks: 5/5 done

<!-- relay:entry 20260919T161824Z-yk author=agent kind=event model=glm-5.3 pane=f997f065 turn=25c2c83c430b4960be9088b8492e2ee5/fd2d0bde4aef48288e8114906071580d -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T161832Z-hp author=agent kind=progress model=glm-5.3 pane=f997f065 turn=25c2c83c430b4960be9088b8492e2ee5/fd2d0bde4aef48288e8114906071580d -->
Landed in 7a5221b (main): PaneBusyLine draws Qt::AlignLeft in the normal weight, its left edge on the prompt text's own edge (the pane hands the line its editor; the inset is read live at paint time as viewport x + document margin, so a theme repolish that changes the padding cannot separate the two rows), sizeHint measures with the painted font, and the stale "bold"/"right-aligned" comments are gone from Pane.h and PaneStatus.h. Verified under Xvfb in all five states plus a 420 px elision — analysis and captures in docs/qa_evidence/2026-09-19-relaying-line-left-normal/. Two notes from a busy shared tree: the worktree was clobbered once by another session's in-flight guest work (the change was re-applied and compiled in an isolated git worktree at the pinned HEAD before committing), and the first copy of this commit (22dc8e6) was undone by that session's `git reset HEAD~1` — 7a5221b is the same tree re-landed on top of their 86440a6. backend-and-bash's ctest timeout is the known credential-prompt hang, documented in the checklist; the retry was stopped at the owner's "no tests until all tasks are done".

<!-- relay:entry 20260919T161836Z-kj author=agent kind=event model=glm-5.3 pane=f997f065 turn=25c2c83c430b4960be9088b8492e2ee5/fd2d0bde4aef48288e8114906071580d -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Implemented in 7a5221b; verified under Xvfb in every state the line can show, evidence in docs/qa_evidence/2026-09-19-relaying-line-left-normal/ · evidence docs/qa_evidence/2026-09-19-relaying-line-left-normal/

<!-- relay:entry 20260919T200629Z-kn author=agent kind=event model=glm-5.3 pane=switchboard turn=e17598d07151419d86ddfea89de60412/c1c5a69cae214f50bedb9384a2802d81 -->
- ✦ agent updated this card · labels: ["change"] → ["change", "feature"]
