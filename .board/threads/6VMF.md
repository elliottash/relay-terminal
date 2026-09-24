<!-- relay:entry 20260924T193614Z-w2 author=agent kind=event model=claude-fable-5-1 pane=3b50a864 turn=5fa1d5a64b8641b8a47f2f79f32a3523/faaec273c9b54a94ae5b08f7d644aa31 -->
- ✦ agent created this card in Planned · issues/features/2026-09-24-start-pane-and-starter-task-recipes-one-first-ru.md

<!-- relay:entry 20260924T193826Z-n2 author=agent kind=event model=claude-fable-5-1 pane=3b50a864 turn=5fa1d5a64b8641b8a47f2f79f32a3523/faaec273c9b54a94ae5b08f7d644aa31 -->
- ✦ agent updated this card · milestone: (unset) → beta; component: (unset) → ["gui", "worker"]; verify: (unset) → {"artifact": "visual", "primary": "script", "also": ["probe", "person"], "human"…; replaced `## Done means`

<!-- relay:entry 20260924T193925Z-pn author=agent kind=event model=claude-fable-5-1 pane=3b50a864 turn=5fa1d5a64b8641b8a47f2f79f32a3523/faaec273c9b54a94ae5b08f7d644aa31 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T203818Z-yy author=agent kind=note model=glm-5.3 pane=03d8a7b5 turn=bb3324c2293c41ca97640b5fd72cdbc9/aabf09688b0448b9a2415cd563bd09fa -->
From #Z82M (Relay Free images, landed in the gateway and desktop today): your "Generate some art" starter recipe should treat image capability as satisfied when EITHER an `openrouter` key is stored OR the Relay Free gateway serves images — `media.catalog()["image"]["relay_free"]["available"]` (it reads `GET /v1/health`'s `images` list, cached 10 min, no token, no registration). On a keyless fresh install that is already true once the operator deploys the `relay-image` role, so the recipe's needs check should not say "needs a provider". The refusal wording your recipe would hit at the daily cap is "Relay Free's daily images are used up. Add your own OpenRouter key in Options › Models › API keys to keep making images." — worth quoting verbatim if the recipe pre-educates about the cap.

<!-- relay:entry 20260924T233906Z-x6 author=agent kind=event model=glm-5.3 pane=751dee4b turn=5fa1d5a64b8641b8a47f2f79f32a3523/5c1410c66b4841ccbddcca2ab5689229 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T233923Z-hn author=agent kind=event model=glm-5.3 pane=751dee4b turn=5fa1d5a64b8641b8a47f2f79f32a3523/5c1410c66b4841ccbddcca2ab5689229 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T233927Z-es author=agent kind=event model=glm-5.3 pane=751dee4b turn=5fa1d5a64b8641b8a47f2f79f32a3523/5c1410c66b4841ccbddcca2ab5689229 -->
- ✦ agent updated this card · verify: {"artifact": "visual", "primary": "script", "also": ["probe", "person"], "human"… → {"artifact": "visual", "primary": "script", "also": ["probe", "person"], "human"…

<!-- relay:entry 20260924T233931Z-4x author=agent kind=decision model=glm-5.3 pane=751dee4b turn=5fa1d5a64b8641b8a47f2f79f32a3523/5c1410c66b4841ccbddcca2ab5689229 -->
Owner, 2026-09-24: "can you add \"start a persistent remote session with multi-pane layout\" as one of the tasks". The starter-task list becomes twelve. The recipe reuses the shipped persistent connect (`ssh.connectPersistent`: mosh + `zellij attach --create`, `ssh -t`/tmux fallbacks) and adds the multi-pane layout as a zellij layout inside the session (shell + editor panes), so the panes live on the host and survive a disconnect; a second run reattaches. Its needs check is an ssh target. Recorded in Done means, Plan step 1 and the verify criteria on this card.
