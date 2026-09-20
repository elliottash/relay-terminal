<!-- relay:entry 20260920T154446Z-a1 author=claude-code kind=event -->
### Claude Code · 2026-09-20 15:44
filed from the terminal: "under apperance / theme, add a "randomize" option"

<!-- relay:entry 20260920T154500Z-a2 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 15:45
claimed this card; adding a Randomize button under Options › Appearance › Theme, with
`relay::theme::randomThemeId()` behind it and the same thing on `/theme random`.

<!-- relay:entry 20260920T160500Z-b1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 16:05
landed. `relay::theme::randomThemeId(avoid)` behind a Randomize button in Options › Appearance,
`/theme random` and a Random row in the `/theme` picker. Unit test
`randomizeNeverGivesYouTheThemeYouAreOn` (300 draws, every other theme comes up, never the one in
use) passes; live screenshots and the drive script are in
`docs/qa_evidence/2026-09-20-randomize-theme/`. Moved to needs-verification with a QA checklist.

<!-- relay:entry 20260920T161500Z-c1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 16:15
landed as `dae8796e` (land.py verify built the exact tree). Screenshots, the drive script and the
run notes are in `docs/qa_evidence/2026-09-20-randomize-theme/`.
