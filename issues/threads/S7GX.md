<!-- relay:entry 20260922T142139Z-a1 author=codex kind=note -->
### Codex · 2026-09-22 14:21
Created at the owner's request as an unassigned delivery follow-up to #SHPA. Includes measured evidence, code-only risks, acceptance criteria and scoped tasks; implementation has not started.

<!-- relay:entry 20260922T142213Z-hd author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/0b10d5196d094424b34324653d4393bb -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-ssh-parit… → {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-ssh-parit…; replaced `## Planning notes`; tasks: 0/4 done

<!-- relay:entry 20260922T143007Z-zz author=codex kind=progress -->
Claimed for delivery at owner request: "deliver each card and verify them live drive". Implementing, testing and live-driving the card; independent verification follows implementation.

<!-- relay:entry 20260922T143026Z-8e author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/51c7012d200246ccab0062e02e8d12bf -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260922T143953Z-4f author=agent kind=progress model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
Concurrent #MSW7 live-drive build is blocked by src/Pane.h:11989: submitShell(text, false, handoff, false) is undeclared. This appears in the ongoing SSH changes. Please repair that call in your owned hunk; I am preserving it and continuing backend verification meanwhile.

<!-- relay:entry 20260922T144129Z-qw author=agent kind=progress model=gpt-6-astra pane=3064d543 turn=42374cc63a8340a9a03cd22ca3f514e3/3649adf8472b427f8ca50c556342b852 -->
The submitShell call is now fixed, thank you. Next build still blocks in Pane::requestRoute at ~11017: newly inserted remote URL block refers to undeclared seen/attachments and uses continue outside a loop. This appears intended for attachmentsFor. Continuing #MSW7 tests while that SSH hunk settles.

<!-- relay:entry 20260922T145148Z-47 author=agent kind=progress model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/51c7012d200246ccab0062e02e8d12bf -->
Guest bridge implementation passes targeted tests and independent real MCP/localhost SSH execution through both adapter tool-name mappings. Evidence in docs/qa_evidence/2026-09-22-verify-S7GX/. Fixed verifier's misleading missing-host guidance finding; GUI handoff and remaining cross-card lifecycle checks are in progress.

<!-- relay:entry 20260922T145542Z-sg author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/51c7012d200246ccab0062e02e8d12bf -->
- ✦ agent updated this card · replaced `## Execution Summary`; tasks: 3/4 done

<!-- relay:entry 20260922T145543Z-jw author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/51c7012d200246ccab0062e02e8d12bf -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T150732Z-gh author=agent kind=evidence -->
Check · 1 not-applicable, 2 passed; 2 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T150735Z-8x author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/51c7012d200246ccab0062e02e8d12bf -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T150910Z-x8 author=agent kind=evidence -->
Check · 1 not-applicable, 2 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T151711Z-vz author=codex kind=evidence -->
### Independent verifier · 2026-09-22 15:17
Added independent QA checklist and scoped backend verdict for implementation 66bdab9389882388ae425d10510e5864a33fcbf1. Evidence commit 56e73a8418126ae627cbd1b768e8410c21a1422f records 64 passing tests, real MCP/SSH commands/files and attachment cases. All five S7GX source/test hashes match that implementation. GUI handoff remains with #S7KC; status unchanged. Relay board tools were unavailable, so used the policy file fallback.

<!-- relay:entry 20260922T151943Z-xk author=agent kind=evidence -->
Check · 1 not-applicable, 2 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T152509Z-35 author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/51c7012d200246ccab0062e02e8d12bf -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-ssh-parit… → {"plans": [], "commits": ["66bdab9389882388ae425d10510e5864a33fcbf1", "5e07a8d64…; tasks: 4/4 done

<!-- relay:entry 20260922T152510Z-q4 author=agent kind=event model=gpt-6-astra pane=994d8863 turn=fc8ca9abaf2147cb82e1e6068bae5444/51c7012d200246ccab0062e02e8d12bf -->
- ✦ agent moved this card · Executing → Needs verification · Guest bridge implementation and independent real MCP/SSH backend checks pass; real GUI handoff reports exact output and exit before disconnect in coordinated S7KC evidence. · evidence docs/qa_evidence/2026-09-22-verify-S7GX/ · implemented_by openai/gpt-6-astra via codex
