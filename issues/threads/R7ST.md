<!-- relay:entry 20260923T180529Z-r7 author=codex kind=note -->
### Codex · 2026-09-23 18:05
Filed the owner's proposed `/restart` command. The existing `/update` relaunches before closing the current process, so the card calls for a common exit-then-launch path and links the saved-layout race #K6KP.

<!-- relay:entry 20260923T181053Z-r8 author=codex kind=progress -->
### Codex · 2026-09-23 18:10
Claimed #R7ST after the owner asked to implement it for the next launch. Plan: use the existing process identity to wait before the replacement initializes, route `/restart` and `/update` through the same clean-quit path, then build and drive an isolated restart.

<!-- relay:entry 20260923T182007Z-r9 author=codex kind=evidence -->
### Codex · 2026-09-23 18:20
The process-identity test and Relay build pass. An isolated Xvfb drive entered `/restart`, saw the old GUI exit, then the replacement start and reopen two tabs with their scrollback files. Evidence: `docs/qa_evidence/2026-09-23-restart-R7ST/`. Moved the card to needs-verification for an independent live review.
