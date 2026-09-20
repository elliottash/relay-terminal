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

<!-- relay:entry 20260920T155459Z-c1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 15:54
Landed in nine commits; evidence in `docs/qa_evidence/2026-09-20-card-vocabulary/`.

Verified on a **clean export**, not in the shared checkout: `git archive 44698c60` into a scratch
tree, configured and built there. Exit 0, `[100%] Built target relay`. That mattered — another
session landed C++ touching `src/RelayWindow.h` and `src/Theme.cpp` after the rename commit, so
the rename's own build gate never saw the combination. 300 Python tests and 4 C++ targets pass.

Three things beyond the original plan, each a loose end the plan would otherwise have created:

- **The guest harness was missed.** `guest_harness_provider.approval_card()` and ~24 ask-sense
  lines across it, the codex adapter and its test. Now `approval_ask()`. Landed as `44698c60`.
- **Two docs still cited the dead symbol** `relay::input::cardTakesRemoteLine`. Landed as `79c4e751`.
- **The web client's "plan cards"** in the remote-protocol table collided with `type: plan`
  Switchboard cards, which is what that phrase means everywhere else in the tree. It is "plan
  blocks" now, matching the CSS class the client already uses. Landed as `ee3569bd`.

Two hunks could not be separated from other sessions' uncommitted work. The README line waited for
`picker-gui` to land and then went in on its own (`e71be553`); the single word in the 12.1
approvals table row was carried into `fallbacks-py`'s own commit `13000e21` between the dry-run
and the confirm. The line is correct on main; that commit carries a word its author did not write.

Moved to needs-verification. Not self-verifying: the QA checklist wants a human or another
provider at the keyboard for the ask, the approval and the guest paths.
