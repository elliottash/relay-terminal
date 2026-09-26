<!-- relay:entry 20260925T124003Z-bd author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · .board/features/2026-09-25-copy-improvement-your-words-are-kept-exactly-as.md

<!-- relay:entry 20260925T124012Z-98 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T124012Z-jn author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T124202Z-vg author=agent kind=event model=glm-5.3 pane=switchboard turn=791050ffaf02c7aa9794f204aa8df398/9d3bd41f8c6a4e2da3db4b46bc20e754 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T124214Z-f1 author=agent kind=event model=glm-5.3 pane=switchboard turn=791050ffaf02c7aa9794f204aa8df398/9d3bd41f8c6a4e2da3db4b46bc20e754 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T124222Z-be author=agent kind=comment mode=plan model=glm-5.3 turn=791050ffaf02c7aa9794f204aa8df398/9d3bd41f8c6a4e2da3db4b46bc20e754 -->
The string lives in `app/board.js`. Let me read the context and hunt for other redundant copy.

Single ask (write the plan), so no todo list needed. Continuing the survey of redundant copy.

The named string is at `app/board.js:1223`, and there's a sibling reassurance at `board.js:1063`. Let me read that context and scan the other user-facing strings in the remote Board page.

One more sweep — the desktop side and the remaining remote strings — before writing the plan.

Desktop `src/` has no matching copy — all hits are code comments. One last check on the desktop Board pane's own composer placeholders for a sibling phrase.

Copy survey done. Let me check whether any test pins these strings before writing the plan.

No test pins these strings. Writing `## Done means`, then the plan.





**Plan written on #DZX8.** It trims the New card placeholder to "What do you want?" and drops the one true sibling it found — "Your words are still here." on the offline refusal in `app/board.js:1063`, which restates the draft still visibly sitting in the box — while recording the strings surveyed and deliberately kept (format hints, the mic note, desktop placeholders). One thing from you, if you care: step 2 is the only judgement call — say so on Run if you'd rather keep that offline sentence.

<!-- relay:entry 20260925T124222Z-bf author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
