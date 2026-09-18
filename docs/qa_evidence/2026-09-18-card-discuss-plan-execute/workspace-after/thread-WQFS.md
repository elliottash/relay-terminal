<!-- relay:entry 20260918T192934Z-np author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260918T193035Z-pb author=agent kind=event model=glm-5.3 pane=switchboard turn=db61aeb31e2d4608be8fb3a6e68c0c2c/a61fedfba43a4563aeb76cfe97e14008 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260918T193041Z-kz author=agent kind=comment mode=plan model=glm-5.3 turn=db61aeb31e2d4608be8fb3a6e68c0c2c/a61fedfba43a4563aeb76cfe97e14008 -->
The repo has no rendering or viewer code at all — only the toy shell's completion module — so this is a greenfield build. Writing the plan to the card:

Plan written to #WQFS. Since the repo has no viewer or pane code today, the plan builds the renderer from scratch: a `src/pdf_pane.py` that extracts and wraps PDF text in-process (with a CLI so it prints in the terminal pane), plus tests proving no external viewer is ever launched. The one decision for you is the `pypdf` dependency (recommended in the plan's Risks); everything else is ready to Execute.
