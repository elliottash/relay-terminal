<!-- relay:entry 20260920T023251Z-by author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-feature-add-a-copy-to-clipboard-button-next-to-t.md

<!-- relay:entry 20260920T041028Z-0v author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T041028Z-n0 author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +2

<!-- relay:entry 20260920T041317Z-g8 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T041317Z-g9 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T041526Z-5q author=agent kind=event model=glm-5.3 pane=switchboard turn=72d116509ed3486f8ae13c6e1fa4b5b1/fe9a5f9f4485439cb83c0290f5c16ad1 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T041530Z-a1 author=agent kind=comment mode=plan model=glm-5.3 turn=72d116509ed3486f8ae13c6e1fa4b5b1/fe9a5f9f4485439cb83c0290f5c16ad1 -->
The matches are in a build tree; let me find the real source.



The pane info is `InfoView` in `src/SessionInfo.cpp`. Let me check existing clipboard usage, the header, and its tests.







Precedent found at `BoardPane.cpp:4722`. Now the test side and header:





One last check — the hints registry and ARCHITECTURE references, to say in the plan whether a hint entry is needed:

I have the full picture. Writing the plan to the card:

**Plan written to #YQC3.** It adds a `relay-info:copy` anchor beside the session id in `sessionHtml()` (and the thread id on thread pages), handles it in `InfoView::linkActivated()` with the clipboard pattern BoardPane already uses, and flashes the view's bottom hint label for feedback, with tests in `conversations_test.cpp` and a live Xvfb check.

One decision for you, noted in the plan's Risks: whether the thread page's id gets the same copy link too (I recommend yes — it's one line), and whether a link-styled `copy` is acceptable as the \"button\" in a QTextBrowser rather than a painted control.

<!-- relay:entry 20260920T041531Z-a1 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260920T042246Z-n5 author=owner kind=comment mode=discuss -->
yes. 

and it should be the standard overlapping boxes copy icon. but clicking on the session id itself also copuies

<!-- relay:entry 20260920T042423Z-03 author=agent kind=event model=glm-5.3 pane=switchboard turn=077ca7d24d31496e901dc05b63b94a29/07795e85c8ab4a8abd655ba6876e027e -->
- ✦ agent updated this card · appended to `## Decisions`; replaced `## Plan`

<!-- relay:entry 20260920T042431Z-p7 author=agent kind=decision model=glm-5.3 pane=switchboard turn=077ca7d24d31496e901dc05b63b94a29/07795e85c8ab4a8abd655ba6876e027e -->
Owner answered the plan's open questions: "yes. 

and it should be the standard overlapping boxes copy icon. but clicking on the session id itself also copuies" — thread id covered too; the affordance is the ⧉ overlapping-boxes copy icon (U+29C9, with a themed-icon fallback if the font lacks it); and the id text itself is a copy link. Plan steps 1-2, 5 and the risks updated accordingly.

<!-- relay:entry 20260920T042441Z-89 author=agent kind=comment mode=discuss model=glm-5.3 turn=077ca7d24d31496e901dc05b63b94a29/07795e85c8ab4a8abd655ba6876e027e -->
All three decisions are on #YQC3 (Decisions section + thread) and folded into the plan:

- **Thread id too** — step 2 stays.
- **Overlapping-boxes icon** — the anchor's label becomes the ⧉ glyph (U+29C9) styled small and muted, with a `QIcon::fromTheme("edit-copy")` PNG fallback if the UI font tofu-boxes it.
- **Clicking the id copies** — the `<code>` id itself is wrapped in the same copy anchor; drag-selection still works.

No open questions left. The card is `planned` — **Execute** hands it to a terminal pane to build.

<!-- relay:entry 20260920T051726Z-4x author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T051726Z-hh author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T051726Z-hi author=owner kind=progress pane=switchboard pane_token=eacfa50b-210f-4633-bdb1-03f224cd89d3 -->
Executing (eacfa50b) · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T051847Z-3k author=agent kind=progress model=glm-5.3 pane=eacfa50b turn=61e67aeae7b74f2bb2992d8fafa7c053/7082b8dfdb3640bb916f418bd0b9cd4c -->
Execute started in the terminal pane: reading the pane source and tests, then implementing the plan (copy anchors in sessionHtml/threadHtml, copy handling in linkActivated, flashHint on the bottom hint label, tests, docs).

<!-- relay:entry 20260920T054712Z-11 author=agent kind=event model=glm-5.3 pane=eacfa50b turn=61e67aeae7b74f2bb2992d8fafa7c053/7082b8dfdb3640bb916f418bd0b9cd4c -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["2fd7fb69", "fb782c01"], "evidence": ["docs/qa_evidenc…; appended to `## QA checklist`

<!-- relay:entry 20260920T054715Z-7r author=agent kind=event model=glm-5.3 pane=eacfa50b turn=61e67aeae7b74f2bb2992d8fafa7c053/7082b8dfdb3640bb916f418bd0b9cd4c -->
- ✦ agent moved this card · Executing → Needs verification · Implemented and landed (2fd7fb69, fb782c01): copy anchors, flashHint, tests, docs; build clean, conversations tests pass, live Xvfb run byte-compared both ids from both click targets. · evidence docs/qa_evidence/2026-09-20-copy-session-id-info-pane/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260920T054724Z-8n author=agent kind=evidence model=glm-5.3 pane=eacfa50b turn=61e67aeae7b74f2bb2992d8fafa7c053/7082b8dfdb3640bb916f418bd0b9cd4c -->
Landed, two commits: 2fd7fb69 (implementation + tests + the ARCHITECTURE half-sentence) and fb782c01 (the Xvfb evidence dir). Card moved to needs-verification with the QA checklist.

What the run proved, beyond the unit tests: the id text and the ⧉ glyph (clicked on its own ink pixels, x=1000 and x=978) each put the exact session/thread id on the clipboard, byte-compared against the saved session and thread files; the hint line flashed "Copied session id/thread id to the clipboard" and restored itself; the ⧉ draws via FreeSerif (fontconfig's pick for U+29C9) — no tofu, so the plan's icon fallback was not needed.

One nuance found while driving (noted in the evidence README): Qt fires a line's anchor even when the click lands in the blank past its last character, so an earlier pass of the harness had "proved" the icon from the snap zone; the committed harness locates the glyph's ink cluster and clicks that. Live sessions always have an id (assigned before the first turn), so the empty-id case stays unit-tested only.
