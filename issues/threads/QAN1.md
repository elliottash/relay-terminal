<!-- relay:entry 20260922T173034Z-yg author=agent kind=progress model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/848666d95d934dcb9e6ab3b6e601b454 -->
Created and claimed via file fallback. Traced structured answers to requestRoute -> answerQuestion -> recordAnswer, which bypasses the normal queue; investigating whether this report concerns a prose question or a submission path bypassing that handler.

<!-- relay:entry 20260922T173034Z-yh author=agent kind=question model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/848666d95d934dcb9e6ab3b6e601b454 -->
1. Were the questions shown with selectable answer options, or written in the agent's normal text reply, and did you submit your answer with Enter or Ctrl+Enter? Recommendation: identify this distinction first; structured asks already have a direct question_answer channel, while prose replies use ordinary prompt routing.

<!-- relay:entry 20260922T173045Z-xq author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/848666d95d934dcb9e6ab3b6e601b454 -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260922T173045Z-xr author=agent kind=event model=gpt-6-astra pane=baae7254 turn=8f65c867dd9d4a8a94e66a0c3929c7b9/848666d95d934dcb9e6ab3b6e601b454 -->
- ✦ agent moved this card · Executing → Discussing · Need question presentation and answer submission gesture to distinguish direct question-answer routing from ordinary prompt queueing.
