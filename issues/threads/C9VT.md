<!-- relay:entry 20260921T020822Z-f1 author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-20-make-it-where-highlight-to-copy-only-works-if-th.md

<!-- relay:entry 20260921T020828Z-2b author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260921T020829Z-j4 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260921T021226Z-6s author=agent kind=event model=kimi-k3 pane=switchboard turn=b40d5834c64249b0a2d241f38fbb7757/f83f0c26f0df46a3a9935a6eeac49a9e -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260921T021237Z-hn author=agent kind=comment mode=plan model=kimi-k3 turn=b40d5834c64249b0a2d241f38fbb7757/f83f0c26f0df46a3a9935a6eeac49a9e -->
All three copy-on-highlight paths are mapped. Writing the plan to the card.

**Done:** the plan is on #C9VT. It adds a "≥3 letters or digits" predicate in `src/CopyOnSelect.h` and gates all three copy-on-highlight paths — the shared pane filter, the terminal engine's `mouseReleaseEvent` (a small duplicate, since `engine/` can't include `src/`), and `RemoteScreen` — with tests added to `tests/copyonselect_test.cpp`.

One thing to confirm before Execute: the plan applies the threshold to PRIMARY as well as the clipboard, reading "highlight to copy only works if…" as the whole gesture — say the word if you want PRIMARY left alone.

<!-- relay:entry 20260921T021237Z-ts author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260921T021445Z-qm author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session f32a3a80

<!-- relay:entry 20260921T021445Z-qn author=owner kind=progress pane=switchboard pane_token=f32a3a80-bff1-4c2d-bc96-d2f5a89f672d -->
Claimed (f32a3a80) · working on it from a terminal pane

<!-- relay:entry 20260921T031937Z-75 author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=f32a3a80 turn=1dab0a361bab4c59a36c3d9979555ab4/4a4b62fe53ad48ce85fdbf11d577423f -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"commits": ["46d7e92a", "23f5525e"], "evidence": ["docs/qa_evidence/2026-09-21-…; appended to `## Execution Summary`

<!-- relay:entry 20260921T032007Z-hs author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=f32a3a80 turn=1dab0a361bab4c59a36c3d9979555ab4/4a4b62fe53ad48ce85fdbf11d577423f -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260921T032031Z-tj author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=f32a3a80 turn=1dab0a361bab4c59a36c3d9979555ab4/4a4b62fe53ad48ce85fdbf11d577423f -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260921T032103Z-f9 author=agent kind=progress model=deepseek/deepseek-v4.1-flash pane=f32a3a80 turn=1dab0a361bab4c59a36c3d9979555ab4/4a4b62fe53ad48ce85fdbf11d577423f -->
Built and tested. `46d7e92a` (code + tests), `23f5525e` (the live drive and its evidence).

The rule is `relay::copyOnSelectWorthCopying()` — three letters or digits or more (`QChar::isLetterOrNumber`), below that neither buffer is written and nothing is toasted. Live under Xvfb with `terminal/copy_on_select=true`: a two-character highlight left both the clipboard and PRIMARY on their sentinel, a three-character one copied both, the long drag still copied, and the explicit copy of those same two characters still worked — **6 passed, 0 failed**, sweep and screenshots in `docs/qa_evidence/2026-09-21-copy-on-highlight-three-chars/`.

**The plan had missed one path, and it mattered.** It named three (the shared pane filter, the engine's PRIMARY write, the remote screen). The terminal pane's *clipboard* half is a fourth: the app-wide event filter in `src/Pane.h` calls `copySelection()` on a left-button release in the terminal. Without gating it, a two-character drag in a terminal pane would still have taken the clipboard — the very case the card is about — because the engine's own release only writes PRIMARY. It is gated now, and since that filter read the selection through a path shared with Ctrl+Shift+C, the gate asks `m_backend->selectedText()` for the rule so the explicit copy is untouched. That is a one-lambda change in `src/Pane.h`; the rest of the pane is untouched, and the other sessions' uncommitted work in that file was left uncommitted (`land.py` landed only my hunk).

The plan's other open question — the threshold applying to PRIMARY as well as the clipboard — I took as settled by the card's own words and by the plan going to Execute without an objection; say the word if you want PRIMARY left alone.

<!-- relay:entry 20260921T032112Z-bs author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=f32a3a80 turn=1dab0a361bab4c59a36c3d9979555ab4/4a4b62fe53ad48ce85fdbf11d577423f -->
- ✦ agent moved this card · Executing → Needs verification · Built, tested and driven live: 46d7e92a (code + tests), 23f5525e (evidence); ctest copyonselect and remotepane green, the Xvfb drive 6 passed / 0 failed · evidence docs/qa_evidence/2026-09-21-copy-on-highlight-three-chars/ · implemented_by deepseek/deepseek-v4.1-flash

<!-- relay:entry 20260921T032241Z-cw author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=f32a3a80 turn=1dab0a361bab4c59a36c3d9979555ab4/4a4b62fe53ad48ce85fdbf11d577423f -->
- ✦ agent updated this card · links: {"commits": ["46d7e92a", "23f5525e"], "evidence": ["docs/qa_evidence/2026-09-21-… → {"commits": ["46d7e92a", "23f5525e", "585bbeec"], "evidence": ["docs/qa_evidence…
