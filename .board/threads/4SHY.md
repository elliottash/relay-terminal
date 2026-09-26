<!-- relay:entry 20260926T023417Z-s8 author=agent kind=event model=gpt-6-luna pane=269e2839 turn=6173581165764472b6c340786cfdcc26/05e1df22a60047abadc535278edb373e -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-enter-and-space-do-not-toggle-model-checkboxes.md

<!-- relay:entry 20260926T023421Z-hc author=agent kind=event model=gpt-6-luna pane=269e2839 turn=6173581165764472b6c340786cfdcc26/05e1df22a60047abadc535278edb373e -->
- ✦ agent claimed this card · assignee agent, Inbox → Running, implemented_by openai/gpt-6-luna via codex:elliott-t-ash-gmail-com, session 269e2839

<!-- relay:entry 20260926T023421Z-hd author=agent kind=progress model=gpt-6-luna pane=269e2839 turn=6173581165764472b6c340786cfdcc26/05e1df22a60047abadc535278edb373e pane_token=269e2839-f83f-41fc-a404-2ac9e9442cf1 -->
Claimed (269e2839) · working on it from a terminal pane

Fix keyboard toggling on Enabled and Pick order; add focused tests and verify.

<!-- relay:entry 20260926T023430Z-0c author=agent kind=event model=gpt-6-luna pane=269e2839 turn=6173581165764472b6c340786cfdcc26/05e1df22a60047abadc535278edb373e -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "none", "sign_off…; replaced `## Done means`

<!-- relay:entry 20260926T023720Z-sj author=agent kind=event model=gpt-6-luna pane=269e2839 turn=6173581165764472b6c340786cfdcc26/9be5c51df90f451c8f5fe08858bb6989 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260926T023740Z-k9 author=agent kind=event model=gpt-6-luna pane=269e2839 turn=6173581165764472b6c340786cfdcc26/9be5c51df90f451c8f5fe08858bb6989 -->
- ✦ agent updated this card · replaced `## Issue`

<!-- relay:entry 20260926T023740Z-sh author=agent kind=rewrite model=gpt-6-luna pane=269e2839 turn=6173581165764472b6c340786cfdcc26/9be5c51df90f451c8f5fe08858bb6989 -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
Enter and Space currently fail to toggle checkable model options on the Models pane’s Enabled and Pick order tabs. Make both keys toggle the checkbox for the current eligible row and add focused regression coverage.

> “enter / space doesnt check / uncheck options in enabled”
> “nor in pick order”
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25
```

</details>

<details><summary>after</summary>

```
Enter and Space do not toggle checkable model options on the Models pane’s Enabled and Pick order tabs. Pick order also mutes some checked rows and marks a model as “current”; the current-model annotation is confusing in configuration tabs, and the muted rows need a clear reason. Make the keys toggle the current eligible row, remove that annotation from hosted configuration tabs, and clarify why rows are muted.

> “enter / space doesnt check / uncheck options in enabled”
> “nor in pick order”
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25
>
> “two weird bugs in \"pick order\": why are some checked items white and some gray? andy why does it say kimi-k3-current?”
> “it says kimi - current in enabled as well. not sure what that is setting because its not supposed to.”
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25
>
> “ditto in effort, its saying kimi -current”
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25
```

</details>

<!-- relay:entry 20260926T023818Z-an author=agent kind=event mention=ZXZV model=gpt-6-luna pane=269e2839 turn=6173581165764472b6c340786cfdcc26/9be5c51df90f451c8f5fe08858bb6989 -->
mentioned in #ZXZV · 2026-09-26 · agent

<!-- relay:entry 20260926T034227Z-hb author=agent kind=event mention=D49C model=claude-opus-5-5 pane=269e2839 turn=6173581165764472b6c340786cfdcc26/5cf2d2908f984b848503f12052a6d41f -->
mentioned in #D49C · 2026-09-26 · agent

<!-- relay:entry 20260926T034443Z-2z author=owner kind=event pane=269e2839 -->
Released (269e2839) · the pane closed

<!-- relay:entry 20260926T034532Z-jk author=agent kind=event model=claude-opus-5-5 pane=cae84571 turn=258ff0e9de5540e09f638ed1541c9168/978abc26046f49f194a85b9aefacbda7 -->
- ✦ agent claimed this card · implemented_by anthropic/claude-opus-5-5 via claude:ashe-ethz-ch, session cae84571

<!-- relay:entry 20260926T034532Z-jl author=agent kind=progress model=claude-opus-5-5 pane=cae84571 turn=258ff0e9de5540e09f638ed1541c9168/978abc26046f49f194a85b9aefacbda7 pane_token=cae84571-aa12-45d5-b3a2-881fb1298087 -->
Claimed (cae84571) · working on it from a terminal pane

Picking up from closed pane 269e2839: Enter/Space toggling on Enabled and Pick order, drop hosted "· current", explain muted Pick order rows.
