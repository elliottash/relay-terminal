<!-- relay:entry 20260925T211244Z-kz author=agent kind=event model=claude-opus-5-5 pane=1948e00e turn=6173581165764472b6c340786cfdcc26/82e12aaa9e394a2c85db01f3095e8848 -->
- ✦ agent created this card in Inbox · .board/features/2026-09-25-models-pane-polish-pick-order-section-headers-an.md

<!-- relay:entry 20260925T213418Z-f5 author=agent kind=event model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/f12975b5ee924855bd3bca2af187a050 -->
- ✦ agent updated this card · appended to `## Planning notes`

<!-- relay:entry 20260925T213503Z-p6 author=agent kind=note model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/f12975b5ee924855bd3bca2af187a050 -->
Fourth item, requested while planning: remove the "x of n enabled…" button under each source in the Sources tab (src/RelayWindowModels.cpp:478, the step-2 link added for card #MDL1), as it duplicates the Enabled tab. Verbatim request: "i want to remove the "x of n enabled" button from sources, since that is duplicative with enabled". Folded into Track C below.

<!-- relay:entry 20260925T213504Z-ps author=agent kind=event model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/f12975b5ee924855bd3bca2af187a050 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260925T213823Z-5v author=agent kind=note model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/f12975b5ee924855bd3bca2af187a050 -->
Fifth item, requested while planning: remove the "when it wants to use a tool" guest-permissions row (src/RelayWindowModels.cpp:490–520) — guests always "just run it" (bypass), matching Relay's own agent per rule 29.1. Verbatim request: "and remove the "when it wants to use a tool" option too, it should always be "just run it"". Scope: delete the choiceRow; simplify `Pane::takeGuestRequest` (src/Pane.h:12238–12259) so `permissions` is no longer staged/read from `guests/<cli>/permissions` (the worker already assumes bypass when absent); clean up the now-dead setting key and any stored `ask`/`deny` values. No other page offers this setting (checked src/RelayWindowSettings.cpp). Folded into Track C below — it rides the Sources-tab removal since both are in the same provider-page block of RelayWindowModels.cpp.

<!-- relay:entry 20260925T213847Z-f0 author=agent kind=event model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/f12975b5ee924855bd3bca2af187a050 -->
- ✦ agent updated this card · appended to `## Planning notes`

<!-- relay:entry 20260925T214756Z-7n author=agent kind=event model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/f36bd17069e2466f8c90c945f4ee6c08 -->
- ✦ agent claimed this card · assignee agent, Discussing → Running, implemented_by kimi/k3, verify defaulted from skill deliver, session 164b2cbb

<!-- relay:entry 20260925T214756Z-7o author=agent kind=progress model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/f36bd17069e2466f8c90c945f4ee6c08 pane_token=164b2cbb-9d52-4505-a13c-e6ce589ec456 -->
Claimed (164b2cbb) · working on it from a terminal pane

Executing the planned workstream with subagents: Track A verification sweep, Track B jobs-as-global-rules (+ #WK7C), Track C polish + removals.

<!-- relay:entry 20260925T214914Z-e8 author=agent kind=event model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/f36bd17069e2466f8c90c945f4ee6c08 -->
- ✦ agent updated this card · title: Models pane polish: pick-order section headers and clearer ranks, tighter effort… → Models pane polish: pick-order headers and ranks, effort spacing, jobs tab as gl…; replaced `## Done means`; tasks: 0/5 done

<!-- relay:entry 20260925T214914Z-ec author=agent kind=rewrite model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/f36bd17069e2466f8c90c945f4ee6c08 -->
- ✦ rewrote title

<details><summary>before</summary>

```
Models pane polish: pick-order section headers and clearer ranks, tighter effort tab, jobs tab as global rules rather than pane-specific
```

</details>

<details><summary>after</summary>

```
Models pane polish: pick-order headers and ranks, effort spacing, jobs tab as global rules, sources cleanups
```

</details>

<!-- relay:entry 20260925T214934Z-z7 author=agent kind=event model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/f36bd17069e2466f8c90c945f4ee6c08 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T222038Z-w1 author=agent kind=comment mode=discuss -->
Track C1 landed in `2db966438ab8bc61e0f9f1ff89a51853286ad0f8`: the per-provider "x of n enabled…" link and the guest "when it wants to use a tool" row are gone from Sources; `Pane::takeGuestRequest` no longer stages or reads `permissions` (absent is the worker's bypass, so guests always just run it — rule 29.1) and stored `guests/<cli>/permissions` values are removed where the row used to read them. Evidence: `docs/qa_evidence/2026-09-25-wbfm-c1/` (Sources screenshot after, driver, fixture worker). Verified with `land.py try --tests "^(settings|modelspane)$"`: builds clean, and every subtest in this area passes; the two remaining failures ("Helper Agent (Alt+Q)" vs "Agent (Alt+Q)") reproduce on pristine main and are not this card's files.
