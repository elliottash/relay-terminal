<!-- relay:entry 20260921T120810Z-c1 author=codex kind=progress -->
### Codex · 2026-09-21 12:08
Claimed; adding Done (d), reusing the acknowledged status-move undo toast. Plan recorded on the card.

<!-- relay:entry 20260921T121055Z-c2 author=codex kind=evidence -->
### Codex · 2026-09-21 12:10
Implemented Done (d) and undo routing. Seven Qt cases passed under isolated Xvfb; tests_check has no findings, failures, or blocking signals. Evidence and QA checklist recorded; moved to needs-verification. Board format check has unrelated existing findings, none for D0NE.

<!-- relay:entry 20260921T121221Z-c3 author=codex kind=evidence -->
### Codex · 2026-09-21 12:12
Landed 832e48498357f3e00f19e67996245873445e3f7c; exact-tree build and ctest -R ^boardpane$ passed. The workspace relay target also builds. Recorded the commit link.
