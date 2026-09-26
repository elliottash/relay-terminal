<!-- relay:entry 20260922T031500Z-p7 author=codex kind=progress -->
### Codex · 2026-09-21 23:15
Claimed this follow-up to #HR5E. Session `19a7fd1b741f433ab903e0aea3b65703` logged a configured plan route from `gpt-5.6-sol` to `glm-5.3`; the global settings still contain `roles/planning/preset=glm-coding` and `roles/planning/effort=max`, so the earlier default-routing fix never applies. I am tracing the setting's lifecycle, defining the migration and shared model-role behavior, then adding backend and GUI regression coverage.

<!-- relay:entry 20260922T031500Z-p8 author=agent kind=event model=gpt-5.6-sol pane=519487c9 turn=5f9e5852a0bf4f4a8a8ff81fc7e532c0/5def37ce41954c2ba3b9d98e6549c93f -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260922T031500Z-p9 author=agent kind=event model=gpt-5.6-sol pane=519487c9 turn=5f9e5852a0bf4f4a8a8ff81fc7e532c0/5def37ce41954c2ba3b9d98e6549c93f -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260922T031500Z-pa author=agent kind=event model=gpt-5.6-sol pane=519487c9 turn=5f9e5852a0bf4f4a8a8ff81fc7e532c0/5def37ce41954c2ba3b9d98e6549c93f -->
- ✦ agent updated this card · tasks: 4/4 done

<!-- relay:entry 20260922T031500Z-pb author=agent kind=event model=gpt-5.6-sol pane=519487c9 turn=5f9e5852a0bf4f4a8a8ff81fc7e532c0/5def37ce41954c2ba3b9d98e6549c93f -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260922T031500Z-pc author=agent kind=event model=gpt-5.6-sol pane=519487c9 turn=5f9e5852a0bf4f4a8a8ff81fc7e532c0/5def37ce41954c2ba3b9d98e6549c93f -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T031500Z-pd author=agent kind=event model=gpt-5.6-sol pane=519487c9 turn=5f9e5852a0bf4f4a8a8ff81fc7e532c0/5def37ce41954c2ba3b9d98e6549c93f -->
- ✦ agent moved this card · Executing → Needs verification · Implemented the one-shot legacy planning-override migration; focused job settings, endpoint/guest plan routing, and the full Relay build pass. · evidence docs/qa_evidence/2026-09-21-phantom-plan-model-PMX7/README.md · implemented_by openai/gpt-5.6-sol via codex

<!-- relay:entry 20260922T031500Z-pe author=agent kind=event model=gpt-5.6-sol pane=519487c9 turn=5f9e5852a0bf4f4a8a8ff81fc7e532c0/5def37ce41954c2ba3b9d98e6549c93f -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-21-phantom-p… → {"plans": [], "commits": ["e654c416f1e1db2c8c512a56bbdf812fc80a0143"], "evidence…
<!-- relay:entry 20260925T225145Z-wg author=agent kind=note -->
Verification (rev 2db96643, clean worktree): PASSED — tests.test_roles + tests.test_plan_turns green (the migration regressions), jobstab 23/1 where the 1 is #E8V1's rename fallout (#SYTR), commit e654c416 an ancestor, implementer evidence present. Evidence: docs/qa_evidence/2026-09-25-verify-PMX7/. Moved needs-verification → needs-qa-llm.

