# PK5Q — The helper agent's model box is the pane's model box

<!-- relay:entry 20260920T000000Z-a1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20
Filed and claimed. The pane's rows are built in `Pane::refreshPickers()`, the helper's in
`relay::helpermodel::fill`; the plan is to lift the pane's construction into `src/ModelRows.{h,cpp}`
and have both boxes call it, then give a helper pick the pane's four kinds (role row, catalog
entry, the `ModelPicker` dialog, the gear) and the pane's three keys.

<!-- relay:entry 20260920T174500Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 17:45
Landed in four commits: `78143918` the shared row builder `relay::modelrows` and its test,
`3669df02` both boxes onto it and the helper's pick path (role rows, catalog entries, the picker
dialog, the gear) with `RolesDialog::writeRoleEntry` and the worker-side refinement for a role
that names one of a provider's other models, `cddde462` the keys (Alt+M, Ctrl+Alt+M, `/model`,
`/models` in every helper prompt box), `b193f83c` the hole the live run found — a helper panel in
a tab nobody has asked anything listed no models at all, because the tab's worker is started at
the first ask; a terminal pane lends its catalog until the helper speaks.

<!-- relay:entry 20260920T175000Z-b2 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 17:50
`docs/qa_evidence/2026-09-20-helper-picker-like-the-pane/` — `drive.sh`, `NOTES.md`, `notes.txt`
and six shots driven live under Xvfb on the binary land.py built from the exact tree it committed.
`_rows-pane.txt` and `_rows-switchboard.txt` are the two popups' rows read off the two shots, and
`03-two-popups-side-by-side.png` is both in one picture.
