<!-- relay:entry 20260920T040313Z-bh author=agent kind=event model=glm-5.3 pane=35c6edbc turn=ab06978005284dd3b52384cbcac85007/db5f3f3e8f5f44ccb10bdd9cb6de4f36 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-20-board-busy-from-the-page-agent-s-own-turn-is-sho.md

<!-- relay:entry 20260920T040339Z-tg author=agent kind=event model=glm-5.3 pane=35c6edbc turn=ab06978005284dd3b52384cbcac85007/db5f3f3e8f5f44ccb10bdd9cb6de4f36 -->
- ✦ agent updated this card · appended to `## Evidence`

<!-- relay:entry 20260924T043029Z-60 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T043029Z-am author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260924T043650Z-jv author=agent kind=event model=kimi-k3 pane=switchboard turn=a9b434879f7ff2a15e741b3e9d7dcfe3/d21f9e9a4a1e4479ac2b8c0b61636336 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T043716Z-hj author=agent kind=event model=kimi-k3 pane=switchboard turn=a9b434879f7ff2a15e741b3e9d7dcfe3/d21f9e9a4a1e4479ac2b8c0b61636336 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T043725Z-gp author=agent kind=comment mode=plan model=kimi-k3 turn=a9b434879f7ff2a15e741b3e9d7dcfe3/d21f9e9a4a1e4479ac2b8c0b61636336 -->
I'll read the card and the code it names.





















Let me check card #0Z13 (the uncap) and where the worker tests for `board_busy` live.

















One more check — the protocol doc's `board_busy` entry, to see whether the contract needs a line.











**Done:** #ZGF5 is planned. The fix is a one-spot change in `src/BoardPane.cpp`'s `board_busy` handler (~7372): show the worker's `text` reason when the event carries it, keeping the current reconstructed message as a fallback for older workers — no worker or web-view change needed, since `app/board.js` already surfaces `text`. Tests extend the existing `board_busy` cases in `tests/boardmodel_test.cpp`; nothing is needed from the owner.

<!-- relay:entry 20260924T043725Z-hr author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
