<!-- relay:entry 20260919T205505Z-4k author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-ctrl-enter-to-continue.md

<!-- relay:entry 20260919T205532Z-pv author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T205532Z-pw author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
ctrl + enter to continue
```

</details>

<details><summary>after</summary>

```
press ctrl + enter to continue, eg if you ran out of turns, or you closed and restarted in progress.
```

</details>

<!-- relay:entry 20260919T205539Z-h8 author=owner kind=comment mode=discuss -->
will this work with the agents?

<!-- relay:entry 20260919T205745Z-a8 author=agent kind=note model=glm-5.3 pane=switchboard turn=0cbbc34299ec4d8e94f68adf080cb328/f3bcce3a04224684a5a7ee07a729c0b7 -->
Answer to "will this work with the agents?" — yes, Continue is already agent-native: `/continue`, the "▸ Continue" link printed at a step/tool-call limit, and the palette's `agent.continue` all send the ordinary prompt `Continue` (`submitAgent("Continue")`, src/Pane.h:8301), which resumes any agent conversation with its history; a guest harness pane restarts with `resume` on the next ask (protocol §26). `agent.continue` has no default key, and Ctrl+Enter (`agent.interrupt`, send-now/interrupt) is a no-op on an empty prompt box — so empty-box Ctrl+Enter = continue is a free slot with no conflict. Scope note: the "ran out of turns" case is one flag the GUI already tracks (`m_limitReached`, Pane.h:15115); the "closed and restarted in progress" case needs a new "turn was cut off" mark, since on reopen the conversation resumes but nothing is retried and `m_limitReached` is not set. If this lands, the standing shortcut-hint rule applies (hint the new Ctrl+Enter path).

<!-- relay:entry 20260919T205752Z-ye author=agent kind=comment mode=discuss model=glm-5.3 turn=0cbbc34299ec4d8e94f68adf080cb328/f3bcce3a04224684a5a7ee07a729c0b7 -->
**Yes — "continue" is already agent-native; the key is the only thing missing.** Answering from the code:

- A "Continue" action already exists for agent conversations: `/continue`, the "▸ Continue" terminal link printed when a turn stops at its step/tool-call limit, and the palette action `agent.continue`. It works by sending the ordinary prompt `Continue` (`submitAgent("Continue")`, `src/Pane.h:8301`), so it resumes any agent conversation with its history — guest harness panes (claude/codex) included, where the next ask restarts the harness with `resume` (protocol §26).
- `agent.continue` has **no default key** today. Ctrl+Enter is bound to `agent.interrupt` ("send now / interrupt and send"), which is a no-op on an **empty** prompt box — so empty-box Ctrl+Enter is a free slot for "continue", no conflict.
- The two cases in the card differ slightly:
  - **Ran out of turns:** the GUI already tracks this (`m_limitReached` on `done {stop_reason: "limit"}`), so empty-box Ctrl+Enter can route straight to the existing Continue. Small change.
  - **Closed and restarted mid-turn:** on reopen the conversation resumes (`state_loaded`) but nothing is retried on its own, and `m_limitReached` is not set — so this half needs a small new "turn was cut off" mark for the same key to cover it.

Let me put that answer on the card:

Noted on **#SXF1**. In short: yes — `/continue` already works with the agents; this card is about giving it the Ctrl+Enter gesture, and the only real work is marking a turn that was cut off by a restart so the same key covers it. If you want it built, **Execute** hands the card to a terminal pane.
