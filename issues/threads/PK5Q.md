# PK5Q — The helper agent's model box is the pane's model box

<!-- relay:entry 20260920T000000Z-a1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20
Filed and claimed. The pane's rows are built in `Pane::refreshPickers()`, the helper's in
`relay::helpermodel::fill`; the plan is to lift the pane's construction into `src/ModelRows.{h,cpp}`
and have both boxes call it, then give a helper pick the pane's four kinds (role row, catalog
entry, the `ModelPicker` dialog, the gear) and the pane's three keys.
