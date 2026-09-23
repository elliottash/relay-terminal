<!-- relay:entry 20260923T154457Z-9h author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-23-ctrl-enter-should-activate-guest-agents-like-typ.md

<!-- relay:entry 20260923T154503Z-15 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260923T154503Z-48 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260923T155233Z-gn author=agent kind=event model=kimi-k3 pane=switchboard turn=77c99f1395e2e72c7ddb8d629291a81c/f7027886cd954221bd747f0a63a35f2f -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260923T155310Z-4c author=agent kind=event model=kimi-k3 pane=switchboard turn=77c99f1395e2e72c7ddb8d629291a81c/f7027886cd954221bd747f0a63a35f2f -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T155320Z-vp author=agent kind=comment mode=plan model=kimi-k3 turn=77c99f1395e2e72c7ddb8d629291a81c/f7027886cd954221bd747f0a63a35f2f -->
I'll plan card #BG2Y. First, let me find the code around guest agents and the composer Enter/Ctrl+Enter handling.

Let me look at how guest agents get activated and where the "first entry" path lives.

Single planning task — no todos needed. Let me look at how a guest pane starts on first input.

Let me read the guest harness code and how a pane starts a guest on the first submission.

The card is terse; let me pin down the exact Enter vs Ctrl+Enter behaviour around a guest in the pane's foreground.







































`src/Pane.h` is too large for the search tool; the `build-clean/` copy is searchable and mirrors it. Let me use that to locate the relevant paths.



Let me read the input routing policy — that's likely where Enter vs Ctrl+Enter destinations are decided.











I have what I need. Reading the card for its current hash, then writing `## Done means`.





**Done:** #BG2Y has a plan. The fix is in `src/Pane.h`: Enter's submit path kicks the deferred guest-harness configure (the "starts on your first prompt" mechanism from #4BPE), while Ctrl+Enter's `interruptAgentWithPrompt()` never does — the plan routes a first Ctrl+Enter through the ordinary submit path when the guest hasn't started, keeping interrupt-while-busy and empty-box "Continue" untouched. Nothing needed from you; it's ready for Execute.

<!-- relay:entry 20260923T155321Z-0p author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
