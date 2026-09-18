<!-- relay:entry 20260918T192023Z-w7 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260918T192101Z-as author=agent kind=event model=glm-5.3 pane=switchboard turn=fa9b3d9add2542ac98ab397b9c1c62f7/6e793887fc0b494fbf47f47bb15fad33 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260918T192107Z-a4 author=agent kind=comment mode=plan model=glm-5.3 turn=fa9b3d9add2542ac98ab397b9c1c62f7/6e793887fc0b494fbf47f47bb15fad33 -->
Found the cause: `src/complete.py::candidates` appends `"/-"` to directories — the `-` is a typo for the intended `/`. Writing the plan.The plan is on #ZW95: the bug is a one-literal typo in `src/complete.py::candidates` (`"/-"` instead of the documented `"/"`), so the fix is one character plus a first `unittest` file to lock it in. No owner decision needed — ready to Execute.

<!-- relay:entry 20260918T192208Z-jm author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260918T192208Z-jn author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260918T192226Z-c5 author=agent kind=event model=glm-5.3 pane=32cd91a0 turn=ded158dacc814dbfb972906bfc2b5c85/41f09d6c3cd042b88329615fed1c41eb -->
- ✦ agent updated this card · implemented_by: (unset) → glm-5.3

<!-- relay:entry 20260918T192406Z-e2 author=agent kind=event model=glm-5.3 pane=32cd91a0 turn=ded158dacc814dbfb972906bfc2b5c85/41f09d6c3cd042b88329615fed1c41eb -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["4b2063a"], "evidence": ["docs/qa_evidence/2026-09-18-…; appended to `## QA checklist`

<!-- relay:entry 20260918T192406Z-e3 author=agent kind=progress model=glm-5.3 pane=32cd91a0 turn=ded158dacc814dbfb972906bfc2b5c85/41f09d6c3cd042b88329615fed1c41eb -->
Executed the plan in commit `4b2063a` (#ZW95 in the message): one-character fix in `src/complete.py::candidates` (`"/-"` → `"/"`), plus the repo's first tests `tests/test_complete.py` (4 unittest cases). All pass; manual check prints `['2026-09-18-EG/']` and `complete("cd 2026-09-1", "2026-09-18-EG/")` yields exactly `cd 2026-09-18-EG/`. Evidence in `docs/qa_evidence/2026-09-18-tab-completion-stray-dash/verify.txt`. Moving to QA.

<!-- relay:entry 20260918T192415Z-zf author=agent kind=event model=glm-5.3 pane=32cd91a0 turn=ded158dacc814dbfb972906bfc2b5c85/41f09d6c3cd042b88329615fed1c41eb -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Fix and tests landed in 4b2063a; all 4 tests pass and the acceptance (path and nothing else) verified manually. QA checklist and evidence on the card. · evidence docs/qa_evidence/2026-09-18-tab-completion-stray-dash/
