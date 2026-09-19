<!-- relay:entry 20260919T153248Z-r7 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Ready → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260919T153248Z-r8 author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260919T153257Z-kq author=agent kind=event model=glm-5.3 pane=c4662e53 turn=ded374951432485287ccc423bed079cd/b3524673833f4b74b712fd791c807998 -->
- ✦ agent updated this card · implemented_by: (unset) → claude-opus-4-5

<!-- relay:entry 20260919T161607Z-gr author=agent kind=event model=glm-5.3 pane=82f45235 turn=ded374951432485287ccc423bed079cd/050f489d959b49de94046fce865913e6 -->
- ✦ agent updated this card · tasks: 5/5 done

<!-- relay:entry 20260919T161612Z-kb author=agent kind=progress model=glm-5.3 pane=82f45235 turn=ded374951432485287ccc423bed079cd/050f489d959b49de94046fce865913e6 -->
Implemented, all five tasks. The start rule now lives in `src/QueueSubmit.{h,cpp}` (`relay::queuesubmit::decide`): an agent prompt starts at once unless the worker reports a turn running or an agent entry has just left the queue without that report yet (the double-start guard). `submitAgent`, `startFix`, `finishHandoff` and `runBoardTask` all go through it — so an agent prompt no longer waits behind a running `sleep` or queued `echo`, and queued items keep their order. `enqueue`'s steer offer now shows only while a turn actually runs. `requestRoute` dispatches an explicit agent submit locally (no `route` round trip, no "Local router is not ready" refusal, no "Input changed during routing" drop); auto and shell still ask the router. `tests/queuesubmit_test.cpp` holds the rule; docs/ARCHITECTURE.md's queue section documents it. Build passes; full suites and the Xvfb evidence run next, then the commit.

<!-- relay:entry 20260919T161619Z-v8 author=agent kind=decision model=glm-5.3 pane=82f45235 turn=ded374951432485287ccc423bed079cd/050f489d959b49de94046fce865913e6 -->
Task d2 — one rule for all three keys, and for every other door into `submitAgent` ("Continue", `/skill`, delegate-program, a phone's prompt, Execute's board task, fix and handoff turns). They are the same submission by another key; a rule that holds for one of the three is a rule the user cannot predict, and the resource argument (the agent contends with no shell) does not depend on the key. The `when: "interrupt"` branch keeps its own meaning: interrupt only when the agent is busy.

<!-- relay:entry 20260919T161627Z-0z author=agent kind=note model=glm-5.3 pane=82f45235 turn=ded374951432485287ccc423bed079cd/050f489d959b49de94046fce865913e6 -->
Trade-off in j1: dispatching an explicit agent submit locally means the router never sees it, so the wrong-mode hint that remembered a runnable command submitted in agent mode (dispatch's `shellText`, suggesting the terminal when its run_command later fails) is gone for forced-agent submits. The card's own reasoning covers this — the router's verdict cannot change where a forced-agent submit goes — and auto-mode submits keep the hint. The route chip reads "AGENT · explicit" on such a submit.

<!-- relay:entry 20260919T163802Z-k6 author=agent kind=event model=glm-5.3 pane=82f45235 turn=ded374951432485287ccc423bed079cd/050f489d959b49de94046fce865913e6 -->
- ✦ agent updated this card · appended to `## QA checklist`; replaced `## Tasks`

<!-- relay:entry 20260919T164121Z-xw author=agent kind=event model=glm-5.3 pane=82f45235 turn=ded374951432485287ccc423bed079cd/050f489d959b49de94046fce865913e6 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["C4M8", "KJ44"], "githu… → {"plans": [], "commits": ["8584b96", "d26cbb4"], "evidence": ["docs/qa_evidence/…

<!-- relay:entry 20260919T164137Z-kk author=agent kind=event model=glm-5.3 pane=82f45235 turn=ded374951432485287ccc423bed079cd/050f489d959b49de94046fce865913e6 -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Implemented and landed (8584b96, d26cbb4): the start rule, the local dispatch, the toast fix, the queuesubmit test, and the Xvfb evidence with the ask timed against the keypress. Ready for QA-LLM. · evidence docs/qa_evidence/2026-09-19-ctrl-enter-sends-now/

<!-- relay:entry 20260919T181500Z-en author=agent kind=note model=claude-fable-5.1 pane=entercheck -->
Follow-on, reported 2026-09-19 by an agent re-shooting the IDE-bridge evidence: with the pane's banner up ("The agent worker exited"), `!bash -c '…'` lit the `! terminal` chip and Enter sent nothing to the shell. **It was not fixed by 8584b96 / d26cbb4, and it reproduces on `main` at `8791cd7`** — live under Xvfb, in an export of main built on its own, `docs/qa_evidence/2026-09-19-terminal-line-with-the-worker-gone/`. Scene by scene: `!echo … > a.txt` with the worker healthy runs; kill the worker, and `!echo … > b.txt` (Enter), `ls` (Enter, AUTO) and `echo … > d.txt` (Ctrl+Shift+Enter) all end at "Local router is not ready; use the native terminal or restart Relay." with the text left in the box and no marker file; the banner's own Restart agent brings all of it back. The cause is the half of j1 that was left behind: `requestRoute` took the explicit *agent* submit off the `route` round trip but kept the `!m_workerReady` gate for `shell`, and the `!` prefix, the TERMINAL chip and Ctrl+Shift+Enter are all `mode == "shell"`. The router is a message to the worker; the shell is not, and a forced-shell line has nothing to ask it — the router's only contribution there is the syntax check that hands a broken command to the agent, and with no agent the shell judges it itself.

<!-- relay:entry 20260919T181501Z-e2 author=agent kind=progress model=claude-fable-5.1 pane=entercheck -->
Fixed and landed. `relay::input::withoutRouter(mode)` (`src/InputPolicy.{h,cpp}`, `tests/inputpolicy_test.cpp`) is the rule: `shell` runs, `agent` takes the agent's path, only `auto` is refused. `requestRoute` dispatches an explicit terminal submit locally through the ordinary `dispatch()` — route chip `TERMINAL · explicit`, the mirror of #N8VK's `AGENT · explicit` — and `auto`'s refusal is now `relay::input::noRouterText`: "The agent worker is not running, so Auto cannot tell a command from a prompt. Restart agent (Ctrl+Shift+R), or press Ctrl+Shift+Enter to run this line in the terminal." The worker-exit handler also clears `m_pendingSubmit`, `m_previewId` and `m_heldDecision`: a `route` in flight when the worker dies is never answered, and that one-submission guard, left behind, silently swallowed every later terminal submit in the pane. Verified as a before/after pair on the same display — same script, main's own build then main plus this change: b, c and d go from nothing to run, run and refused-with-the-way-out.
