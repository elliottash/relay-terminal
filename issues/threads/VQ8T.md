<!-- relay:entry 20260920T153406Z-a1 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 15:34
Surveyed every sense of "card" in the tree: eight distinct ones, plus `discard`/`wildcard` as
grep hazards. Put the survey and a recommendation to the owner, who answered *"i agree with these
recommendations."* The decisions are in the card's `## Decisions`.

The load-bearing finding: the question/approval prompt is not a card and never was. `Pane::printQuestion`
prints inline terminal text in `Ink::Ask` — no border, no surface, no widget. The code around it already
says "ask" in every name (`m_ask`, `Ink::Ask`, `ask_user`, `approvals_ask`), and the wire events are
`question`/`question_closed`/`question_answer`. Nothing on the protocol says card. The word was
introduced by sessions writing comments; the owner's own words in the source cards say "approvals",
"ask" and "checklist".

<!-- relay:entry 20260920T153406Z-a2 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 15:34
Claimed. Four tasks: the renames and visible strings (x1), the `src/`+`backend/` reword (x2), the
`docs/` reword including retiring "turn cards" (x3), and the naming rule in `WARP.md` (x4).
x1 and x2 both touch `src/Pane.h`, so they go to one implementer; x3 and x4 are disjoint from them.
