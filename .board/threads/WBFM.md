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

<!-- relay:entry 20260925T222312Z-2v author=agent kind=event mention=D4BJ model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/7dbfd40692e043f0b4ab6d09e876fd2c -->
mentioned in #D4BJ · 2026-09-25 · agent

<!-- relay:entry 20260925T222630Z-qw author=agent kind=event mention=EQH0 model=claude-opus-5-5 pane=cd39c532 turn=eb540dbc335b48608bf140f752a963ce/2533e01880b34962874cd7d74c7d59de -->
mentioned in #EQH0 · 2026-09-25 · agent

<!-- relay:entry 20260925T223500Z-a7 author=relay kind=progress -->
Track C2 landed (#WBFM): pick order's high/main/flash rows are real section headers — taller than their rows, bold +1pt name, muted note, and a muted "N ranked" count over the rank column that restarts at 1 under each header (headers take no number); rank numbers stay the stored per-tier ranks, ties shown as equal numbers, matching the draw semantics the consumers use. Effort rows tightened 66/48 → 44px by sizing from the font instead of padded constants. Commits: 7f2e2b22e713 (src/ModelPicker.cpp), bf6d99c50a02 (evidence). Evidence: docs/qa_evidence/2026-09-25-wbfm-c2/ (before/after + compare montages, drive harness). relay-modelpicker-tests 62/62; relay-modelspane-tests 25 passed + 1 pre-existing #E8V1 label failure. Card not moved.

<!-- relay:entry 20260925T225929Z-g0 author=agent kind=event mention=SYTR model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/7aeccffbcbab4e24b0c8129df98ba560 -->
mentioned in #SYTR · 2026-09-25 · agent

<!-- relay:entry 20260925T234223Z-98 author=owner kind=event pane=164b2cbb -->
Released (164b2cbb) · the pane closed

<!-- relay:entry 20260926T005435Z-ze author=agent kind=event model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["2db966438ab8", "b3ace8921408", "7f2e2b22e713", "bf6d9… → {"plans": [], "commits": ["2db966438ab8", "b3ace8921408", "7f2e2b22e713", "bf6d9…; appended to `## Tests`; tasks: 5/5 done

<!-- relay:entry 20260926T005510Z-tw author=agent kind=event model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260926T005531Z-36 author=agent kind=event model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260926T005558Z-js author=agent kind=evidence -->
Check · 1 not-applicable; 2 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260926T005704Z-r7 author=agent kind=event model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260926T005746Z-g4 author=agent kind=event model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
- ✦ agent moved this card · Running → Needs verification, wrote `## Execution Summary` · Tracks A, B, C1 and C2 landed. The exact-tree build passed; targeted test results include the pre-existing #SYTR rename mismatch, now linked to that card. · evidence docs/qa_evidence/2026-09-25-wbfm-b/ · implemented_by openai/gpt-6-luna via codex

<!-- relay:entry 20260926T010042Z-gp author=agent kind=evidence model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
Continuation verification at current shared-checkout HEAD d30cb637 (source includes #WBFM/#WK7C commit ff7c872db2e6): `scripts/relay-build --target relay-modelpicker-tests relay-modelspane-tests relay-jobstab-tests relay-appcommands-tests relay-settings-tests` passed. `ctest --test-dir build -R '^(modelpicker|modelspane|jobstab|appcommands|settings)$' --output-on-failure`: modelpicker, jobstab, appcommands passed; modelspane and settings each have one failure, both the already tracked #SYTR stale expectation `Helper Agent (Alt+Q)` vs current `Agent (Alt+Q)`. The #WK7C cases in jobstab/appcommands pass. This confirms the earlier `tests_run` output was on pre-Track-B revision 8659c576 but the remaining two failures are real at current HEAD and already tracked by #SYTR.

<!-- relay:entry 20260926T010043Z-pr author=agent kind=event mention=WK7C model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
mentioned in #WK7C · 2026-09-26 · agent

<!-- relay:entry 20260926T024350Z-pn author=agent kind=event model=gpt-6-luna pane=269e2839 turn=6173581165764472b6c340786cfdcc26/143b678f74d84352b3262d835f7d4591 -->
- ✦ agent claimed this card · Needs verification → Running, implemented_by openai/gpt-6-luna via codex:elliott-t-ash-gmail-com, session 269e2839

<!-- relay:entry 20260926T024350Z-po author=agent kind=progress model=gpt-6-luna pane=269e2839 turn=6173581165764472b6c340786cfdcc26/143b678f74d84352b3262d835f7d4591 pane_token=269e2839-f83f-41fc-a404-2ac9e9442cf1 -->
Claimed (269e2839) · working on it from a terminal pane

Resume the Jobs tab portion: remove the pane-specific live report so Job rules presents only the shared rules it edits, per the owner’s clarification.

<!-- relay:entry 20260926T025852Z-k8 author=agent kind=evidence model=gpt-6-sol pane=269e2839 turn=6173581165764472b6c340786cfdcc26/de5eab23456c4a58ac8f4647bb032d56 -->
Current-model trace (2026-09-25): ModelsPane passes its served pane's currentEntryKey into ModelPicker; Enabled/Pick order/Effort append '· current' for that key. It is a pane state label, not a global selection. New pane startEntry draws among usable Main rank-1 ties; routing-draws.jsonl records kimi-code|k3 chosen at 22:31:15 EDT for pane 6ad44757 (26.2% draw probability), followed by configure_sent; no model_pick for that event. Remaining first-row/sticky paths: mainDefault and ModelRows::modeKey use first usable row for display/swap; RoleResolver.summary resolves High/Flash without a draw; PaneSession stores every non-main model_changed as a per-pane mode pick, reused on later mode switches. User asks to resolve current-model framing and rank-1 selection across Main/High/Flash.

<!-- relay:entry 20260926T034227Z-d7 author=agent kind=event mention=D49C model=claude-opus-5-5 pane=269e2839 turn=6173581165764472b6c340786cfdcc26/5cf2d2908f984b848503f12052a6d41f -->
mentioned in #D49C · 2026-09-26 · agent

<!-- relay:entry 20260926T034443Z-13 author=owner kind=event pane=269e2839 -->
Released (269e2839) · the pane closed

<!-- relay:entry 20260926T034531Z-nf author=agent kind=event model=claude-opus-5-5 pane=cae84571 turn=258ff0e9de5540e09f638ed1541c9168/978abc26046f49f194a85b9aefacbda7 -->
- ✦ agent claimed this card · implemented_by anthropic/claude-opus-5-5 via claude:ashe-ethz-ch, session cae84571

<!-- relay:entry 20260926T034531Z-ng author=agent kind=progress model=claude-opus-5-5 pane=cae84571 turn=258ff0e9de5540e09f638ed1541c9168/978abc26046f49f194a85b9aefacbda7 pane_token=cae84571-aa12-45d5-b3a2-881fb1298087 -->
Claimed (cae84571) · working on it from a terminal pane

Picking up from closed pane 269e2839: finish the Jobs tab follow-up (drop the pane-specific live report so Job rules shows only the shared rules), then back to needs-verification.
