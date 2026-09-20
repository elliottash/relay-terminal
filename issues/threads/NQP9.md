<!-- relay:entry 20260919T224552Z-92 author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-when-board-is-done-planning-send-a-notification.md

<!-- relay:entry 20260919T224645Z-6m author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T224645Z-w7 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
when board is done planning, send a notification, but dont instantly move the active pane there.
```

</details>

<details><summary>after</summary>

```
when board is done planning, send a notification, but dont instantly move the active pane there.

do we have a hotkey to,  move to most recent notification? if not we should add that, maybe f1 for example.  and you can press f1 again to move to the 2nd most recent notification, etc.
```

</details>

<!-- relay:entry 20260919T224646Z-zf author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260919T225652Z-eg author=agent kind=event model=glm-5.3 pane=switchboard turn=96579b1ac5c24aca844ac759efab269a/6375d7662f2c428cbe14d5097a40128a -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260919T225704Z-r8 author=agent kind=comment mode=plan model=glm-5.3 turn=96579b1ac5c24aca844ac759efab269a/6375d7662f2c428cbe14d5097a40128a -->
Now let me check the test layout and the hint registry so the plan can name verification targets.













I have the full picture now. Writing the plan to the card.

**Done:** the plan is on #NQP9. Two parts: the Switchboard posts a "Plan ready: #ID" entry to the bell when a card's Plan turn ends (never moving the active pane — today it does neither, so this is purely additive), and a new `notifications.jump` keymap action cycles through notifications newest-first on repeat presses, with the bell click kept as the mouse path.

One decision for you: you suggested F1, but F1 is already the shortcuts list and `docs/F-KEYS.md` reserves F-keys for toggles — the plan recommends **Ctrl+Shift+M** instead. Say so on the card if you want a different key before executing.

<!-- relay:entry 20260920T040903Z-76 author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T040911Z-ze author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +2

<!-- relay:entry 20260920T041139Z-m2 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T041139Z-m3 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T041213Z-mc author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T041213Z-md author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T041214Z-xj author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T052030Z-k7 author=agent kind=note model=kimi-k2.7-code-highspeed -->
- ✦ the jump key is **Ctrl+Shift+1**, not the plan's Ctrl+Shift+M: `agent.resume` took M on 2026-09-19 ("Manage Sessions"), after the plan's survey, and no Ctrl+Shift+letter is free — D/P/Y are claimed in the preset tables, C/V are terminal copy/paste, Q "quits other terminals", U is the input method's Unicode entry (the reasons recorded beside `ssh.connect` in `src/Keymap.h`). Asked the owner; he said continue without picking, so the recommendation stands: the digit counts the walk (1 = the newest, again for the 2nd), and the Ctrl+Shift chord still acts while a program owns the terminal (`Keymap::actsInsidePrograms`). One line in `src/Keymap.h` rebinds it.

<!-- relay:entry 20260920T052040Z-q2 author=agent kind=progress model=kimi-k2.7-code-highspeed -->
- ✦ implemented: `NotificationCenter::markSeen(id)`; `BoardView::onTurnEnded(id, mode, outcome)` fired when a card turn ends (done/error/cancelled, mode read before the turn is dropped; cleanups are not card turns and never reach it); `createBoardPane` posts "Plan ready: #ID" / "Plan failed: #ID" with the card title as the body and source `board:<workspace>#<id>` — no focus call anywhere on the posting path; `openNotificationSource` routes tokens to `focusPane` as before and `board:` sources to the Switchboard on that workspace (this window first, another window's raised) via `revealBoardCard`, or opens one beside the active leaf; `notifications.jump` (Ctrl+Shift+1) walks newest-first with a ~4 s reset, marks only the entry landed on seen, and the popup's row click teaches the key. `scripts/relay-build` green; `ctest -R notifications` passed; `tests/test_keybindings.py` 19/19; Xvfb smoke clean. Evidence: `docs/qa_evidence/2026-09-20-board-plan-notification/`
