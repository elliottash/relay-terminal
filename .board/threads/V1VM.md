<!-- relay:entry 20260924T031949Z-q7 author=agent kind=event model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
- ✦ agent created this card in Planned · issues/features/2026-09-23-collect-usage-resets-credits-and-window-reset-ti.md

<!-- relay:entry 20260924T031959Z-d0 author=agent kind=note model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
Follow-up from the user's terminal: "claude has usage rests now, so we should start tracking those in my crontab as well as add the functionality in relay" — so the ask spans (a) Relay's usage stats and (b) the user's crontab scanner at /home/elliott/data/usage_tracker/check_usage.py (outside this repo).

<!-- relay:entry 20260924T031959Z-ts author=agent kind=event model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by glm/glm-5.3, session 9af6a7f5

<!-- relay:entry 20260924T031959Z-tt author=agent kind=progress model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 pane_token=9af6a7f5-a6df-450e-9953-621582632fbf -->
Claimed (9af6a7f5) · working on it from a terminal pane

Investigated the scanner and both providers' endpoints; implementing: Codex usage-reset credits through the usage_limits wire into the picker/Sources text, plus per-window reset coverage, plus the same tracking in the crontab scanner.

<!-- relay:entry 20260924T032007Z-rh author=agent kind=event model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": ["probe"], "human": "none", "c…; appended to `## Done means`

<!-- relay:entry 20260924T032022Z-mb author=agent kind=event model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260924T034019Z-q3 author=agent kind=event model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260924T034035Z-e7 author=agent kind=event model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260924T034043Z-0b author=agent kind=event model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented in f607e5e9 plus the crontab scanner patch; unit and catalog tests green, live scanner run green; UI change needs eyes. · evidence docs/qa_evidence/2026-09-24-usage-resets/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260924T034310Z-v1 author=agent kind=progress model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260924T034409Z-2x author=agent kind=note model=glm-5.3 pane=9af6a7f5 turn=90b3725007bf4d71860a6b377e948a26/58fc1b08cdc042c5afca2f631e14ba93 -->
Try it could not be staged: the picker's `· N usage resets` line needs a live Codex login reporting `usage_limits`, and a disposable fixture cannot complete Codex's OAuth flow (the other limit sources need API keys the sandbox lacks). Everything machine-checkable is automated in `relay-modelcatalog-tests` — the same seam #XH4K's evidence used — and the scanner half is already person-visible live: the sheet's new column H shows `5h reset` for the Claude rows and `usage_resets=1` for both Codex logins since the 2026-09-24 00:02 run. In their real session the check is: after the next Codex turn, the picker row reads e.g. `weekly 64% left, resets 21:20 · 1 usage reset`. Note in `docs/qa_evidence/2026-09-23-tryit-V1VM/could-not-stage.md`.
