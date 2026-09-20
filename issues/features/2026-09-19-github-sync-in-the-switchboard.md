---
id: ZKR0
type: work
status: planned
labels: [feature]
component: [gui]
milestone: beta
workstream: switchboard
rank: '39'
created: '2026-09-19'
acceptance: from the Switchboard, a first sync shows its dry-run plan and needs a confirmation above the create cap; a conflict is visible on the card; a rate limit says when to retry
source: 'follows #GDQN: the engine (`f878b6a`) and the worker messages (protocol 19.14) are on `main` with no UI'
links: {plans: [], commits: [], evidence: [], related: [JN7X], github: null}
---
# The Switchboard shows the GitHub sync: the plan before the first run, progress, and conflicts

## Issue

`forge_sync_plan` / `forge_sync_run` exist and nothing in the GUI sends them. The engine is safe to
drive headless; this card is the part a person uses.

## Tasks

- [ ] a Sync action in the Switchboard header and the palette, shown only when `board.yaml` has <!-- t:a4 -->
      `github: {repo}` or the project's primary remote is GitHub (`project_probe`)
- [ ] the first run always shows `forge_sync_planned` (creates, updates, closes, comments per side) <!-- t:rg -->
      and sends `confirm_bulk` only after an explicit yes when `needs_confirm` is set
- [ ] `forge_sync_progress` as a quiet line; `forge_sync_done` as a summary; `retry_at_text` verbatim <!-- t:wt -->
- [ ] conflicts: the thread entry the engine writes is already on the card — mark the card in the <!-- t:yf -->
      board and offer "keep mine / keep GitHub's" per field
- [ ] the init question's "Sync with GitHub issues" box records `github: {repo}` and never syncs <!-- t:qb -->
- [ ] where the token came from, shown once ("using `gh auth token`"), never the token <!-- t:w6 -->
- [ ] shortcut hint for the Sync key, if it gets one <!-- t:j1 -->

## Not here

GitLab, Gitea and Forgejo providers (`ForgeProvider` is the seam; build one when somebody asks).
Jira, Linear and Azure DevOps stay link-out only (`docs/PROJECT-INIT-AND-IMPORT.md`).
