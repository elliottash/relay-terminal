<!-- relay:entry 20260924T234625Z-gw author=agent kind=event model=glm-5.3 pane=10f88b29 turn=6237f4b6cc8b4c559a13dfecb7e329fa/4a8ce0590c6d4e46bc4b3e2b3e8a32af -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-24-two-projectinit-tests-are-stale-at-head-initcomm.md

<!-- relay:entry 20260925T052654Z-vv author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T052654Z-vw author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T052931Z-0f author=agent kind=event model=glm-5.3 pane=switchboard turn=beb61d230fe2e02e3e70bfc8cdd0381b/c440081ee57b4f9784e6507316ffa79f -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T052945Z-h8 author=agent kind=event model=glm-5.3 pane=switchboard turn=beb61d230fe2e02e3e70bfc8cdd0381b/c440081ee57b4f9784e6507316ffa79f -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T052949Z-cx author=agent kind=comment mode=plan model=glm-5.3 turn=beb61d230fe2e02e3e70bfc8cdd0381b/c440081ee57b4f9784e6507316ffa79f -->
I'll read the card, then the test file and the sources it checks.

Let me read the middle section with the two failing tests in full.



This is one linear ask (plan the card), so no todo list needed. Now let me find where the expected strings actually live now.

The strings moved into the `.cpp` files (card #243T split). Let me check exactly which of the tests' other anchors still live in the headers.

Pane.h in the current tree has none of those anchors — the tree has moved since the card's measurement. Let me map where each test anchor lives now.



The tree has drifted well past the card's measurement — `handleBoardInitRequest` etc. may have been renamed. Let me see what the project-init wiring in Pane looks like now.

The working tree has drifted far past the card's measurement (Pane's init wiring has since moved again — e.g. `board_init_request` is now in `src/BoardRemote.cpp`). Let me look at the two anchor sites to write accurate findings.

One more check — how the test binary is registered so the plan can name the verify step exactly.

I have the full picture. Writing `## Done means` first, then the plan.



**Plan written on #FV0P.** It fixes the two stale tests by re-pointing them from `src/Pane.h`/`src/RelayWindow.h` at the `.cpp` files the #243T body-split moved the wiring into — keeping every assertion, none weakened — and it opens with a re-measure-at-HEAD step because the tree has drifted further since the card was measured (the Pane init wiring now shows up in `src/BoardRemote.cpp` and `src/RelayWindowCore.cpp`), so the failure list at Run time may be longer than two. Nothing needs from you; the one owner-decision path is flagged in Risks: if a pinned anchor turns out to be removed rather than moved, the Run agent stops and asks on the card instead of deleting the assertion.

<!-- relay:entry 20260925T052949Z-fa author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260926T003548Z-4s author=agent kind=event mention=3MH4 model=claude-fable-5-1 pane=f35051fe turn=dbc9af56def4427c91f4fecd2d277da4/1a6acee91a96495087b757d1ef3ee4e7 -->
mentioned in #3MH4 · 2026-09-26 · agent
