<!-- relay:entry 20260925T150249Z-f7 author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-no-find-function-in-cards-ctrl-f-does-nothing-on.md

<!-- relay:entry 20260925T150255Z-rr author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by glm/glm-5.3, session b2ca06b3

<!-- relay:entry 20260925T150255Z-x4 author=agent kind=progress model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 pane_token=b2ca06b3-0efd-42ab-95fa-f0d2c3196ca6 -->
Claimed (b2ca06b3) · working on it from a terminal pane

Plan: give the card page a find strip over its document (QTextBrowser::find, next/prev/wrap, match count), route the keymap's find.inView (Ctrl+F) to the Board when the keyboard is on it — card page finds in the open card, list page focuses the existing filter — and prove it with an offscreen boardfind test like boardfocus.

<!-- relay:entry 20260925T150319Z-dg author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "none", "criteria…

<!-- relay:entry 20260925T151231Z-5h author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260925T151239Z-xz author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260925T151245Z-68 author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"commits": ["dcf3fbbcf866"], "evidence": ["tests/boardfind_test.cpp"]}; appended to `## Tests`

<!-- relay:entry 20260925T151311Z-2z author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260925T151347Z-9k author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260925T153639Z-7c author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260925T153658Z-vg author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented and landed (dcf3fbbcf866, plus the consolemode guard 6c6f75ef66ed for the signal this work opened); boardfind test green, neighbours green, verify-slot build green. Needs independent verification per the QA ladder. · evidence tests/boardfind_test.cpp · implemented_by glm/glm-5.3

<!-- relay:entry 20260925T153704Z-tp author=agent kind=progress model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260925T155011Z-f2 author=agent kind=event model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
- ✦ agent updated this card · replaced `## Try it`

<!-- relay:entry 20260925T155021Z-t0 author=agent kind=evidence model=glm-5.3 pane=b2ca06b3 turn=69cb9fd7cf1a4fcaa96aa0bfe14b5305/06d6c4ea22b340d7893cc8a58e0c8135 -->
Try it staged by the implementer (no verifying-session staging to reuse). Evidence: `docs/qa_evidence/2026-09-25-tryit-9NBZ/` — `stage.sh` seeds a disposable board (four cards; the Parallax card repeats *parallax* in three sections) and opens this checkout's `build/relay` on it under a throwaway profile.

The mechanical pass ran under Xvfb through `scripts/relay-drive` named controls (`open P4RA`, `action find.inView`, `type boardCardFind`, `press boardFindNext`/`boardFindClose`); path: 8 steps, all agent, 0 person — the only human step is the section's question. What I saw in the real window: the strip opens over the document, typing `parallax` lands on the first match and reads **3 matches**, Enter steps 2nd → 3rd → wraps to 1st, `unfindableword` reads **No matches**, and the close button hides the strip (consecutive screenshots differ at every step; OCR of the strip confirms the counts).

![card open, Ctrl+F, first of 3 matches](docs/qa_evidence/2026-09-25-tryit-9NBZ/04-first-match.png)
![a word not in the card reads No matches](docs/qa_evidence/2026-09-25-tryit-9NBZ/07-no-matches.png)

Two small follow-on commits landed while staging this: the strip's buttons are named controls (`bc1fda0f`) and `boardCardFind` joined the drive typable allowlist (`cce7e8a0`) — both needed for the board's own drive discipline, boardfind still green after each.
