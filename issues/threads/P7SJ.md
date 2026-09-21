<!-- relay:entry 20260921T220533Z-a1 author=codex kind=progress -->
### Codex · 2026-09-21 22:05
Captured and took responsibility for this design discussion after inspecting the existing picker,
Sessions manager, Options registry rows, and HQ card #Y2MP. Related implementation is card #916B;
consistent grouping is card #TVE1. This card stays discussing: no application code changed.

<!-- relay:entry 20260921T220533Z-a2 author=codex kind=decision -->
### Codex · 2026-09-21 22:05
Owner: "i am liking the middle option. can you include a \"globals\" tab that has our proposed \"swithcboard hq\" functionality"

Owner: "and ctrl shift g can open globals"

Owner: "i dont need the pane screenshot key, not sure why i have that"

Recorded the shared Projects / Sessions / Globals pane direction, Globals on Ctrl+Shift+G,
and removal of the conflicting screenshot default binding. Added the proposed tab responsibilities,
source-backed research, current reuse points, and the remaining HQ dependency and preset conflict.


<!-- relay:entry 20260921T220927Z-p1 author=codex kind=progress -->
### Codex · 2026-09-21 22:09
Claimed for implementation with three subagents, a written plan and bounded file ownership. Owner: "lets go with subagents for efficiency". Using the proposed memory defaults (pinned/path-matched, bounded, project precedence), existing instruction sources, no team store and no global loose-card fallback.

<!-- relay:entry 20260921T222440Z-q1 author=codex kind=evidence -->
### Codex · 2026-09-21 22:24
Implemented with three subagents and landed integration be42269896bc. Targeted backend/widget/keymap tests and exact-tree application build pass. Isolated Xvfb verifies all three shortcut tabs, a saved global memory consumed by runtime and draft preservation. Evidence: docs/qa_evidence/2026-09-21-projects-sessions-globals/. Added execution summary, tests and manual QA checklist; moved to needs-verification.

<!-- relay:entry 20260921T233253Z-r1 author=codex kind=decision -->
### Codex · 2026-09-21 23:32
Owner: "fix it, remove that old project page and move ctrl shift s back to switchboard". Reclaimed #P7SJ; removing the legacy picker and routing selection through Projects. Ctrl+Shift+S will open Switchboard, including for a directory without a board, without creating files merely by opening it.

<!-- relay:entry 20260921T233735Z-r2 author=codex kind=evidence -->
### Codex · 2026-09-21 23:37
Landed 188b5931: Ctrl+Shift+S opens Switchboard directly and the legacy picker is removed. Exact-tree build and 11 init tests pass. Isolated live GUI shows fully loaded empty Switchboard from a loose folder, no filesystem initialization, no worker protocol error. Evidence: docs/qa_evidence/2026-09-21-projects-sessions-globals/07-switchboard-direct.png. Returned to needs-verification.
