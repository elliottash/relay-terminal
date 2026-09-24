<!-- relay:entry 20260922T173034Z-yg author=agent kind=progress model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/848666d95d934dcb9e6ab3b6e601b454 -->
Created and claimed via file fallback. Traced structured answers to requestRoute -> answerQuestion -> recordAnswer, which bypasses the normal queue; investigating whether this report concerns a prose question or a submission path bypassing that handler.

<!-- relay:entry 20260922T173034Z-yh author=agent kind=question model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/848666d95d934dcb9e6ab3b6e601b454 -->
1. Were the questions shown with selectable answer options, or written in the agent's normal text reply, and did you submit your answer with Enter or Ctrl+Enter? Recommendation: identify this distinction first; structured asks already have a direct question_answer channel, while prose replies use ordinary prompt routing.

<!-- relay:entry 20260922T173045Z-xq author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/848666d95d934dcb9e6ab3b6e601b454 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260922T173045Z-xr author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/848666d95d934dcb9e6ab3b6e601b454 -->
- ✦ agent moved this card · Executing → Discussing · Need question presentation and answer submission gesture to distinguish direct question-answer routing from ordinary prompt queueing.

<!-- relay:entry 20260922T173610Z-y7 author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/ca4dfc45a7264fb7a0708e6bc5e59bf1 -->
- ✦ agent updated this card · waiting_on: owner → (unset); appended to `## Decisions`

<!-- relay:entry 20260922T173610Z-y8 author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/ca4dfc45a7264fb7a0708e6bc5e59bf1 -->
- ✦ agent moved this card · Discussing → Executing · Resuming the existing claim to analyze all question and submission paths at the owner's request. · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260922T173659Z-v4 author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/ca4dfc45a7264fb7a0708e6bc5e59bf1 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260922T174237Z-st author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/ca4dfc45a7264fb7a0708e6bc5e59bf1 -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260922T174253Z-kk author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/ca4dfc45a7264fb7a0708e6bc5e59bf1 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T174312Z-z7 author=agent kind=evidence -->
Check · 1 missing-evidence, 1 not-applicable, 3 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T174348Z-xy author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/ca4dfc45a7264fb7a0708e6bc5e59bf1 -->
- ✦ agent moved this card · Executing → Needs verification · Structured/prose ordering regressions, guest round trips and phone browser tests pass; independent verification pending. Test-coverage warning references only the earlier intake commit and will clear when implementation lands. · evidence docs/qa_evidence/2026-09-22-question-answers/ · implemented_by openai/gpt-6-astra via codex

<!-- relay:entry 20260922T174350Z-99 author=agent kind=evidence -->
Check · 1 not-applicable, 4 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T174526Z-m8 author=agent kind=evidence -->
Check · 1 not-applicable, 4 passed. The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T174552Z-h9 author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/ca4dfc45a7264fb7a0708e6bc5e59bf1 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-question-… → {"plans": [], "commits": ["cf473cd53b74016b741ae8730831caefc4a0495e"], "evidence…; appended to `## Execution Summary`

<!-- relay:entry 20260923T025141Z-rt author=agent kind=evidence -->
Check · 1 failed, 1 not-applicable, 3 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T232104Z-d6 author=agent kind=evidence -->
Check · 1 not-applicable, 4 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T232343Z-a5 author=agent kind=evidence -->
Check · 1 not-applicable, 4 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.
