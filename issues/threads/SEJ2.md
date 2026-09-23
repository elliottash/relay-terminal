<!-- relay:entry 20260920T223634Z-qa author=agent kind=event model=kimi-k3 pane=ae279c67 turn=52b448974b7a4de2a20c7664163566bd/fe5f9616486c4aca938ed391c2a8c725 -->
- ✦ agent created this card in Discussing · issues/design/2026-09-20-ctrl-enter-or-shift-enter-for-open-external-vs-e.md

<!-- relay:entry 20260920T223826Z-r7 author=agent kind=event model=kimi-k3 pane=ae279c67 turn=52b448974b7a4de2a20c7664163566bd/fe5f9616486c4aca938ed391c2a8c725 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; links: {"plans": [], "commits": [], "evidence": [], "related": ["V9V1"], "github": null… → {"related": ["V9V1"]}; appended to `## Discussion points`

<!-- relay:entry 20260920T223841Z-kj author=agent kind=question model=kimi-k3 pane=ae279c67 turn=52b448974b7a4de2a20c7664163566bd/fe5f9616486c4aca938ed391c2a8c725 -->
Which mapping should the file explorer use? Recommendation: **Ctrl+Enter = edit here (in-app variant), Shift+Enter = open external (leaves Relay)** — see Discussion points for the research: Ctrl means "parallel variant, same app" (VS Code open-to-side, Firefox new tab, Relay's own Ctrl+Enter = act here now) and Shift means "elsewhere / bigger departure" (Firefox new window, Shift+click, Relay's Shift+Enter = open in a new pane). Options: (a) recommended mapping with both bindings; (b) only bind Shift+Enter = open external for now; (c) the reverse mapping (Ctrl+Enter external).

<!-- relay:entry 20260920T224007Z-c0 author=agent kind=decision model=kimi-k3 pane=ae279c67 turn=52b448974b7a4de2a20c7664163566bd/4198c0ee4f5d4d38be6bc9d1da0387db -->
"i agree" — the recommended mapping stands: Ctrl+Enter = edit here (in-app variant), Shift+Enter = open external, Enter = open internal. Scope extended: "in the relay text editor, we also need to add an edit button."

<!-- relay:entry 20260920T224033Z-wm author=agent kind=event model=kimi-k3 pane=ae279c67 turn=52b448974b7a4de2a20c7664163566bd/4198c0ee4f5d4d38be6bc9d1da0387db -->
- ✦ agent claimed this card · assignee agent, Discussing → Executing, implemented_by kimi/kimi-k3, session ae279c67

<!-- relay:entry 20260920T224033Z-wn author=agent kind=progress model=kimi-k3 pane=ae279c67 turn=52b448974b7a4de2a20c7664163566bd/4198c0ee4f5d4d38be6bc9d1da0387db pane_token=ae279c67-818d-44f1-b7ad-54d566c8b795 -->
Claimed (ae279c67) · working on it from a terminal pane

Decision made: Ctrl+Enter = edit here, Shift+Enter = open external, plus an edit button in the text viewer. Scoping FilePanes/viewers, then writing the plan onto the card.

<!-- relay:entry 20260920T224608Z-eq author=agent kind=event model=kimi-k3 pane=ae279c67 turn=52b448974b7a4de2a20c7664163566bd/4198c0ee4f5d4d38be6bc9d1da0387db -->
- ✦ agent updated this card · replaced `## Plan`; tasks: 0/7 done

<!-- relay:entry 20260920T231315Z-gk author=owner kind=event pane=ae279c67 -->
Released (ae279c67) · the pane closed

<!-- relay:entry 20260921T114033Z-z5 author=agent kind=progress -->
The #SEJ2 code was sitting uncommitted in the shared checkout. Landed unchanged on 2026-09-21 as `5dfec93d` after land.py's build gate built the exact tree and `ctest -R '^filepanes$'` passed in it (owner: clean the tree). The commit is the explorer's Ctrl+Enter/Shift+Enter chords, `FilePreview::startEditing()`, the local QSaveFile save, the dirty guards in `open()` and `closePane()`, the header's ✎ button and the new cases in `tests/filepanes_test.cpp` — tasks t:xy, t:cq, t:ey, t:p5 and t:qa. Not landed with it: the "Open external" menu hint (t:7j, only the ✎ button's hint was written) and the Xvfb evidence run (t:tp).

<!-- relay:entry 20260922T044022Z-09 author=owner kind=comment mode=discuss -->
Is this done and can be moved to verify?

<!-- relay:entry 20260922T044118Z-yk author=agent kind=event model=kimi-k3 pane=switchboard turn=d76d24967cb6b3231a88774b5cda7215/03b26882cd2841d1b682ed26c28e90f8 -->
- ✦ agent updated this card · tasks: 5/7 done

<!-- relay:entry 20260922T044128Z-8y author=agent kind=comment mode=discuss model=kimi-k3 turn=d76d24967cb6b3231a88774b5cda7215/03b26882cd2841d1b682ed26c28e90f8 -->
Let me check what's actually in the tree before answering — particularly the missing menu hint (t:7j).











The code confirms the thread: the chords, ✎ button, save, and guards are in, but the "Open external" menu hint isn't (no hint fires from the `openExternal` menu path). Let me sync the task list with what actually landed.



**Not quite — 5 of 7 tasks are done, two remain.** I checked the tree and updated the task list on #SEJ2 to match.

Landed in `5dfec93d` (build-gated, `ctest -R '^filepanes$'` passed at land time): the Ctrl+Enter/Shift+Enter chords, `startEditing()`, local QSaveFile save, both dirty guards, the ✎ Edit button, and the tests.

Still open, both needing a terminal pane (Execute):

1. **t:7j** — the menu-click "Open external" → "Next time: Shift+Enter" hint was never written (only the ✎ button's hint landed). A few lines in `FilePanes.cpp`.
2. **t:tp** — the Xvfb live evidence run into `docs/qa_evidence/2026-09-20-edit-here/`. A large card moves to needs-verification with its evidence path, so this is the gate for your question.

I'd hold the move until those two land: press **Execute** and a pane can finish both in one short session, then the card goes to verify. If you'd rather drop the menu hint as not worth it, say so and I'll mark t:7j dropped — but the evidence run still has to happen before verification.
