<!-- relay:entry 20260920T215207Z-a5 author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-20-if-i-did-ctrl-alt-m-in-the-switchboard-it-moved.md

<!-- relay:entry 20260920T215229Z-q5 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T215229Z-q6 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
if i did ctrl alt m in the switchboard, it moved me to a pane
```

</details>

<details><summary>after</summary>

```
if i did ctrl alt m in the switchboard, it moved me to a pane. it shouldnt do that. it should ideally use that for the switchboard agent. but otherwise just dont do anything.
```

</details>

<!-- relay:entry 20260920T215231Z-x1 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T215231Z-x2 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T220044Z-ja author=agent kind=event model=kimi-k3 pane=switchboard turn=7c690ffcba464b779d4f51463fc20651/e3ee2922561d4ba698697356c5aa6ce8 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T220103Z-7s author=agent kind=comment mode=plan model=kimi-k3 turn=7c690ffcba464b779d4f51463fc20651/e3ee2922561d4ba698697356c5aa6ce8 -->
**Plan written to #GZAE.** The bug is in `runActionNow` (src/RelayWindow.h ~1141): Ctrl+Alt+M only routes to the helper when the helper composer has focus; otherwise it fires `openModelPicker()` on `m_active`'s terminal pane, which drags you out of the Switchboard. The plan widens that branch so the key drives the Switchboard agent's model picker/box whenever the active leaf is the board (Alt+M included), falls back to doing nothing otherwise, and starts with an Xvfb reproduction to confirm exactly which focus case misfires. Nothing needed from you — it's ready for Execute.

<!-- relay:entry 20260920T220103Z-d6 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260922T043733Z-3m author=owner kind=comment mode=discuss -->
I bet this is stale

<!-- relay:entry 20260922T044132Z-bh author=agent kind=note model=kimi-k3 pane=switchboard turn=7e925771d837b9ca7684018a4161b209/c7bf9d6e437141afb4c364329caf58ab -->
Stale check (owner: "I bet this is stale") — **mostly confirmed.** The plan's entire mechanism is gone: #AGNT (2026-09-21, needs-verification, 40 commits) deleted `HelperChatPanel`, `HelperModelBox`, `focusedHelperModelBox()`, `openHelperModelPicker()`, `openHelperModelBox()` and `helperModelState()` — every function and line number this plan names. The bug itself is described in the current code in past tense: `src/RelayWindow.h:1535,1547` — "clicking into the Switchboard's composer and pressing Alt+M dropped open the *terminal* pane's model box … It used to ask [the pane]" — and #AGNT's commit `c4a1625d` is literally "step 5 — … Alt+M, and the two the live gate caught". Now every prompt box is a `Pane` console with its own model box, and the dispatch (`runActionNow` :1280) routes `agent.modelBox`/`agent.modelOptions` to `focusedConsole()` when a console has focus. One residue unproven either way: with the Switchboard up but focus in the **card list** (not the composer), the helper branch still misses and the key falls to `pane->openModelBox()` on `m_active` (:1296) — whether that still yanks the user off the board needs a live Xvfb drive, which is a Verify/Execute step, not this plan.

<!-- relay:entry 20260922T044144Z-mk author=agent kind=question model=kimi-k3 pane=switchboard turn=7e925771d837b9ca7684018a4161b209/c7bf9d6e437141afb4c364329caf58ab -->
The plan is stale but the card-list focus arm is unverified. Close #GZAE as superseded by #AGNT (letting #AGNT's verification cover it), or keep the card as a slim residual check — "with the Switchboard up and focus in the card list, Alt+M/Ctrl+Alt+M must not jump to a terminal pane" — for a live drive? **Recommended:** keep it, rewrite to the residual check, and verify it alongside #AGNT's QA — it's one Xvfb scene and it's the exact scenario the owner hit.

<!-- relay:entry 20260922T044152Z-8h author=agent kind=event model=kimi-k3 pane=switchboard turn=7e925771d837b9ca7684018a4161b209/c7bf9d6e437141afb4c364329caf58ab -->
- ✦ agent moved this card · Planned → Discussing · Plan confirmed stale (#AGNT deleted its mechanism); waiting on owner: close as superseded or keep as residual card-list focus check.

<!-- relay:entry 20260922T044212Z-a0 author=agent kind=event model=kimi-k3 pane=switchboard turn=7e925771d837b9ca7684018a4161b209/c7bf9d6e437141afb4c364329caf58ab -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"related": ["AGNT"]}; waiting_on: (unset) → owner

<!-- relay:entry 20260922T044229Z-04 author=agent kind=comment mode=discuss model=kimi-k3 turn=7e925771d837b9ca7684018a4161b209/c7bf9d6e437141afb4c364329caf58ab -->
Checking the current code to see whether the bug the plan describes is still there.

The code has moved quite a bit since the plan was written — let me read the current dispatch region.











The code has clearly been reworked since the plan. Let me check whether another card already delivered this fix.







The picture is clear. Let me record the findings on the card.











**You're right — #GZAE is stale.** I checked the tree and moved it Planned → Discussing with the findings on the card:

- The plan's entire mechanism is gone. #AGNT (2026-09-21) deleted `HelperChatPanel`, `HelperModelBox`, `focusedHelperModelBox()`, `openHelperModelPicker()` — every function and line number the plan names.
- The bug itself is described in the current code in past tense (`src/RelayWindow.h:1535`: "clicking into the Switchboard's composer and pressing Alt+M dropped open the *terminal* pane's model box … it used to"), and #AGNT's commit `c4a1625d` is literally "Alt+M, and the two the live gate caught". Every prompt box is now a `Pane` console with its own model box.
- **One residue unproven:** with the Switchboard up but focus in the *card list* (not the composer), the key still falls through to `pane->openModelBox()` on `m_active` — whether that still yanks you off the board needs a live drive.

Question on the card: close it as superseded by #AGNT, or keep it slimmed down to that residual card-list check? My recommendation is keep-and-verify alongside #AGNT's QA — it's the exact scenario you hit.

<!-- relay:entry 20260925T124834Z-76 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Discussing → Dropped · moved from iOS Safari
