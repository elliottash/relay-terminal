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

<!-- relay:entry 20260920T024539Z-1t author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T025112Z-yt author=agent kind=event model=glm-5.3 pane=switchboard turn=aa63e2079a07462ba10d703b3dc973c4/9feb8bd2147a416b866da4608b815f45 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T025117Z-04 author=agent kind=comment mode=plan model=glm-5.3 turn=aa63e2079a07462ba10d703b3dc973c4/9feb8bd2147a416b866da4608b815f45 -->
The card's thread already did the scoping; now let me pin down the exact code for the plan.









The routing point is clear (`Pane::interruptAgentWithPrompt`, Pane.h:906–915: empty box → "Type a prompt first."). Let me pin down the continueTurn hint text and the `state_loaded` handler shape.



















The session file's checkpoints carry an `ended` field — that looks like the persisted "turn was cut off" signal. Let me verify.

`checkpoints.span()` already documents the "turn still running" case. Now: when is the session file written mid-turn? Let me see the save path around agent.py:2595–2630 and who calls it.

Mid-turn autosave exists (`MID_TURN_SAVE_S = 10.0`), so a killed turn *is* in the session file with a checkpoint lacking `ended`. Two last checks: does the worker emit `ready` after `state_loaded` (ordering for clearing the GUI flag), and the exact `state_loaded` payload.













I have everything verified. Writing the plan onto the card.

Planned on **#SXF1**: Ctrl+Enter on an empty prompt box routes to the existing `continueTurn()`, gated on the two cases in the card — `m_limitReached` for the limit case, and a new `m_turnCutOff` the worker derives on `resume` from the checkpoint `ended` stamps already persisted in the session file (`state_loaded` gains a `turn_open` field, so the protocol doc gets one line). No binding changes, so send-now on a non-empty box is untouched, and the `/continue` slow path teaches the new key per the standing hint rule.

Nothing needs your input — **Execute** hands it to a terminal pane as-is.

<!-- relay:entry 20260920T031437Z-w8 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T031437Z-w9 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T031450Z-c4 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T031450Z-xz author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T031451Z-04 author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T031503Z-g8 author=agent kind=progress model=glm-5.3 pane=12e7d518 turn=1d5a748e0a184aed8ec5e87d05a45f3d/fb585b44e0944b56a58267d7782abbae -->
Starting execution of the plan: worker predicate + state_loaded field, GUI flag + routing, hint surfaces, tests, build.

<!-- relay:entry 20260920T032205Z-8d author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T033547Z-6a author=agent kind=event model=glm-5.3 pane=fae91646 turn=0feb685b80de4da58a4644ed44f89145/56f0bdb111b74e5ca67669777cbd16b2 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"commits": ["d9cde60524af4b8def16916aa505875d321299de"], "evidence": [], "githu…

<!-- relay:entry 20260920T033547Z-vr author=agent kind=progress model=glm-5.3 pane=fae91646 turn=0feb685b80de4da58a4644ed44f89145/56f0bdb111b74e5ca67669777cbd16b2 -->
Worker half landed as d9cde605: `conv_index.turn_left_open` (pure cut-off predicate, `session_unfinished` now ORs it in), `Agent.resume` → `state_loaded {turn_open}`, protocol §5 + event list, and tests — predicate (finished/cut-off/waiting-message/legacy), resume event (open, completed, pre-`ended` legacy). The commit's own verify gate ran the four cases green in the exact landed tree. Note: a previous execution pane had already written the worker edits into the tree uncommitted; I claimed them with land.py `--from-head` and landed them, selecting only this card's two protocol-doc hunks (two other uncommitted doc hunks — #BRD3's and another session's — were left in the tree for their owners). Next: GUI flag + routing + surfaces, then build and ctest.

<!-- relay:entry 20260920T033913Z-9r author=agent kind=event model=glm-5.3 pane=fae91646 turn=0feb685b80de4da58a4644ed44f89145/56f0bdb111b74e5ca67669777cbd16b2 -->
- ✦ agent updated this card · links: {"commits": ["d9cde60524af4b8def16916aa505875d321299de"], "evidence": [], "githu… → {"commits": ["d9cde60524af4b8def16916aa505875d321299de", "472ae1a210a4e0577d690c…

<!-- relay:entry 20260920T033913Z-t5 author=agent kind=progress model=glm-5.3 pane=fae91646 turn=0feb685b80de4da58a4644ed44f89145/56f0bdb111b74e5ca67669777cbd16b2 -->
GUI half landed as 472ae1a2: `m_turnCutOff` (set from `state_loaded {turn_open}`, cleared with `m_limitReached` and on every done), routing in `interruptAgentWithPrompt` via the extracted `relay::continueturn::sendNowContinues` rule (new `src/ContinueTurn.h` + `tests/continueturn_test.cpp`, the queuesubmit pattern — the plan's "drive a Pane" harness does not exist headless, so the rule was extracted to be testable), and the surfaces: Keymap descriptions, the `/continue` slow-path hint and ▸ Continue link now teach `agent.interrupt`'s first binding, palette row matches. land.py's build gate caught a missing `relay::` namespace qualifier before anything landed; after the fix the exact tree built and `ctest -R continueturn` passed. Next and last: the live Xvfb check (limit stop, kill-and-restart, hint) with evidence.

<!-- relay:entry 20260920T053526Z-dm author=agent kind=event model=glm-5.3 pane=5822ded8 turn=0feb685b80de4da58a4644ed44f89145/0d0f0dedafd44b489c226e8d8ffaa178 -->
- ✦ agent updated this card · links: {"commits": ["d9cde60524af4b8def16916aa505875d321299de", "472ae1a210a4e0577d690c… → {"commits": ["d9cde60524af4b8def16916aa505875d321299de", "472ae1a210a4e0577d690c…; appended to `## QA checklist`

<!-- relay:entry 20260920T053533Z-qx author=agent kind=progress model=glm-5.3 pane=5822ded8 turn=0feb685b80de4da58a4644ed44f89145/0d0f0dedafd44b489c226e8d8ffaa178 -->
Live check complete, ALL PASS at tip c0af1554 (this pane picked up the finish after fae91646's turn ended): re-ran the full Xvfb drive against `build/relay` — A limit-stop Ctrl+Enter sends `Continue`; B kill -9 mid-turn → relaunch → "Session loaded" → Ctrl+Enter sends `Continue` (worker had sent `state_loaded {turn_open: true}`); C `/continue` shows the "Next time: Ctrl+Return · continue a stopped turn from an empty prompt box" hint. Evidence + harness under docs/qa_evidence/2026-09-20-ctrl-enter-continue/. Three harness traps found and fixed in drive.py (a lock deadlock that froze the mock mid-record; the hung turn must complete one tool step first because the mid-turn autosave only fires at step boundaries; the limit line's own hints open the 20 s ShortcutHints global gap that silently swallowed `continue.slow`). Re-ran targeted tests in the current tree: `ctest -R continueturn` 1/1, the three Python cases (turn_left_open, session_unfinished, resume turn_open) 3/3 OK.

<!-- relay:entry 20260920T053534Z-6t author=agent kind=note model=glm-5.3 pane=5822ded8 turn=0feb685b80de4da58a4644ed44f89145/0d0f0dedafd44b489c226e8d8ffaa178 -->
Owner reported during execution: "sometimes it still says 'type a prompt first'" and "ctrl+enter still doesn't do continue in an idle pane". Checked: the instance the owner was typing in (pid 118616) was started 2026-09-19 15:54 — about eight hours before the three commits landed (23:39–23:48) — so it predates the feature entirely; a restart of Relay picks it up. Also by design (the plan's scope decision): Ctrl+Enter continues only a turn that stopped at its limit or was cut off by a restart — after a normally finished turn, an Esc stop, or a kill before the turn's first autosave, the empty box keeps "Type a prompt first." (nothing is left open to continue).

<!-- relay:entry 20260920T053539Z-9w author=agent kind=event model=glm-5.3 pane=5822ded8 turn=0feb685b80de4da58a4644ed44f89145/0d0f0dedafd44b489c226e8d8ffaa178 -->
- ✦ agent moved this card · Executing → Needs verification · All three plan surfaces landed (d9cde605, 472ae1a2, 78416e39) and the live Xvfb check passes every case: limit stop, kill-and-restart, and the teaching hint; targeted tests green in the current tree. · evidence docs/qa_evidence/2026-09-20-ctrl-enter-continue/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260920T053628Z-v8 author=agent kind=event model=glm-5.3 pane=5822ded8 turn=0feb685b80de4da58a4644ed44f89145/0d0f0dedafd44b489c226e8d8ffaa178 -->
- ✦ agent updated this card · links: {"commits": ["d9cde60524af4b8def16916aa505875d321299de", "472ae1a210a4e0577d690c… → {"commits": ["d9cde60524af4b8def16916aa505875d321299de", "472ae1a210a4e0577d690c…

<!-- relay:entry 20260920T231202Z-52 author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=dc5c54ef turn=17696e58a6664868a01d40f294d6b917/98decbcdc31d4c17a6332b55d5bfcd59 -->
- ✦ agent claimed this card · Needs verification → Executing, implemented_by deepseek/deepseek-v4.1-flash, session dc5c54ef

<!-- relay:entry 20260920T231202Z-gp author=agent kind=progress model=deepseek/deepseek-v4.1-flash pane=dc5c54ef turn=17696e58a6664868a01d40f294d6b917/98decbcdc31d4c17a6332b55d5bfcd59 pane_token=dc5c54ef-dc70-4eaa-8564-ebd061b9126b -->
Claimed (dc5c54ef) · working on it from a terminal pane

Owner changed the rule: Ctrl+Enter on an empty prompt box always sends the agent prompt "Continue" — dropping the limit/cut-off gate this card landed. Changing ContinueTurn.h's rule, the routing, the Keymap/doc wording and the test, then re-checking live.

<!-- relay:entry 20260920T234458Z-pb author=agent kind=decision model=deepseek/deepseek-v4.1-flash pane=4ebfcd2d turn=17696e58a6664868a01d40f294d6b917/de3a2e6f9a404dbb8efb638f780c4c55 -->
Owner, in the terminal pane: "ctrl+enter in an empty prompt should always send agent prompt 'continue'" — so the gate this card first landed is dropped: an empty prompt box with an idle agent sends `Continue` whatever the last turn did, not only after a limit stop or a cut-off restart. Text in the box and a busy agent are unchanged; a password prompt is the one exception (the masked field stands in for the prompt box there).

<!-- relay:entry 20260920T234528Z-mk author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=4ebfcd2d turn=17696e58a6664868a01d40f294d6b917/de3a2e6f9a404dbb8efb638f780c4c55 -->
- ✦ agent updated this card · appended to `## Decisions`; replaced `## QA checklist`

<!-- relay:entry 20260920T234532Z-2b author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=4ebfcd2d turn=17696e58a6664868a01d40f294d6b917/de3a2e6f9a404dbb8efb638f780c4c55 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260920T234534Z-e5 author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=4ebfcd2d turn=17696e58a6664868a01d40f294d6b917/de3a2e6f9a404dbb8efb638f780c4c55 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260920T234536Z-hs author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=4ebfcd2d turn=17696e58a6664868a01d40f294d6b917/de3a2e6f9a404dbb8efb638f780c4c55 -->
- ✦ agent updated this card · links: {"commits": ["d9cde60524af4b8def16916aa505875d321299de", "472ae1a210a4e0577d690c… → {"commits": ["d9cde60524af4b8def16916aa505875d321299de", "472ae1a210a4e0577d690c…

<!-- relay:entry 20260920T234605Z-j0 author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=4ebfcd2d turn=17696e58a6664868a01d40f294d6b917/de3a2e6f9a404dbb8efb638f780c4c55 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260920T234819Z-e6 author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=4ebfcd2d turn=17696e58a6664868a01d40f294d6b917/de3a2e6f9a404dbb8efb638f780c4c55 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260920T234836Z-zb author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=4ebfcd2d turn=17696e58a6664868a01d40f294d6b917/de3a2e6f9a404dbb8efb638f780c4c55 -->
- ✦ agent moved this card · unchanged · Owner's rule landed in e7b1e4be: Ctrl+Enter on an empty prompt box always sends the agent prompt Continue (the limit/cut-off gate is gone). Live Xvfb check ALL PASS — an ordinary finished turn continues, typed text is still sent as itself, the limit case still works, /continue still teaches Ctrl+Return, a busy agent still sends nothing; evidence in docs/qa_evidence/2026-09-20-ctrl-enter-always/, tests_check clean, ctest -R continueturn and the three worker tests green. · evidence docs/qa_evidence/2026-09-20-ctrl-enter-always/ · implemented_by deepseek/deepseek-v4.1-flash

<!-- relay:entry 20260920T234840Z-5y author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=4ebfcd2d turn=17696e58a6664868a01d40f294d6b917/de3a2e6f9a404dbb8efb638f780c4c55 -->
- ✦ agent moved this card · Executing → Needs verification · Landed e7b1e4be + evidence ad14646d: Ctrl+Enter on an empty prompt box always sends Continue (the limit/cut-off gate is gone). Live Xvfb check ALL PASS, tests_check clean, ctest -R continueturn and the three worker tests green. · evidence docs/qa_evidence/2026-09-20-ctrl-enter-always/ · implemented_by deepseek/deepseek-v4.1-flash

<!-- relay:entry 20260922T044939Z-gd author=agent kind=evidence -->
Check · 1 not-applicable, 4 passed. The status is under `## Tests`; earlier checks are in this thread.
