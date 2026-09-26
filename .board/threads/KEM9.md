<!-- relay:entry 20260925T124059Z-7v author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · .board/features/2026-09-25-add-the-plan-button-and-remove-the-discuss-selec.md

<!-- relay:entry 20260925T124137Z-wv author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T124137Z-ww author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T124617Z-s7 author=agent kind=event model=glm-5.3 pane=switchboard turn=eb8023b2756d7dd623b9a3efe23a2a4f/65abeb08ea8a4a54bc1519cc456677ee -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T124635Z-pb author=agent kind=event model=glm-5.3 pane=switchboard turn=eb8023b2756d7dd623b9a3efe23a2a4f/65abeb08ea8a4a54bc1519cc456677ee -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T124642Z-fb author=agent kind=comment mode=plan model=glm-5.3 turn=eb8023b2756d7dd623b9a3efe23a2a4f/65abeb08ea8a4a54bc1519cc456677ee -->
The card is about the phone (remote web) card composer. There's a recent change card about it — let me read that and the actual code.









The desktop's card page already has the exact shape the owner wants (box = discuss, a Plan button on the row). Let me confirm how the desktop box handles comment, so the phone can match it.



The desktop pattern is confirmed (owner decision #VZ69: Enter discusses, Ctrl+Enter plans, Ctrl+Shift+Enter comments; Plan is a row button). Now the phone-side functions in detail:











One more check — how the phone web app's UI changes are tested here:











I have the full picture now — re-reading the reply row source once more to anchor exact line references for the plan:





I have everything I need. Re-reading the card for its current hash, then writing the two sections:





Planned #KEM9: the phone's reply row becomes `mic · Stop · Plan · Send` — the mode select goes, Send Discusses (empty Send still resumes), Plan queues like the desktop's, and comment-only survives as the Ctrl+Shift+Enter chord; `tests/test_board_view.py`'s `pick()` helper reroutes to the new buttons so the contract tests keep their meaning.

One decision waits in Risks: comment-only loses its touch affordance (answering still works by just sending the message), so an **offline** phone can no longer queue a comment — fine to ship without a third button?

<!-- relay:entry 20260925T124642Z-fc author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
