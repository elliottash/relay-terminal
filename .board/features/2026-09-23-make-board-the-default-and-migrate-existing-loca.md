---
id: GRT2
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
implemented_by: kimi/kimi-k3
session: 297817d3-877f-4125-a584-deb56f03d1b5
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-23'
verify: {artifact: code, primary: script, also: [], human: optional, criteria: relay-board.py check runs against .board with no rename-caused errors; git ls-files issues = 0 and .board = 1208; Board pane opens the migrated board; other four project boards open at .board with their cards intact, sign_off: none, effort: medium}
source: owner in Relay pane, 2026-09-23
links: {plans: [], commits: [9bd89b7cfc21e81576a5e38e61c8fce3e2841908], evidence: [docs/qa_evidence/2026-09-23-board-migration-GRT2/README.md, docs/qa_evidence/2026-09-23-board-migration-GRT2/], related: [1CXD], github: null}
---
# Make .board the default and migrate existing local project boards

## Issue
i think its worth it in terms of not interfering with the human files. so lets migrate to .board, first in the setup code, then the other projects on my machine, and we will migrate this folder last

## Done means
New Relay projects create `.board/`; existing folders still open until explicitly moved. Other real local project boards move without losing cards, threads, Git tracking or agent pointers; this repository's `issues/` board moves last. A newly created and each migrated board is discoverable by the GUI, worker, CLI and agent instructions. A failed check or active writer stops the affected migration without losing its original files.

## Plan
**Goal.** New project Boards use `.board/`; existing board names still load. Migrate local project boards after the code works, with this repository's `issues/` moved last.

**Findings.** Backend discovery/defaults live in `backend/relay_core/board.py` and `board_tools.py`; GUI discovery/defaults in `src/Projects.h/.cpp`; setup and rename UI in `src/ProjectInit.cpp`, `BoardSections.cpp`, `BoardPane.cpp`; the CLI uses `BOARD_FOLDERS`. The project registry is `~/.local/share/relay/state/projects.json`. Other detected boards include wily, sweet-street, tracelaw, and a Dropbox website project; some have uncommitted work.

**Steps.** 1. Add `.board/` as the first recognized/default folder in backend and GUI, preserve older spellings, update setup/rename wording and tests. 2. Build and run focused board/project tests using an isolated project. 3. Inventory each real local board and instructions; migrate clean boards with an atomic rename and update generated policy/pointers and Git attributes; preserve any concurrent edits. 4. Move this repository's `issues/` last, updating its explicit paths, hooks and documents, then verify board tools, builds and index. 5. Land only this work using `scripts/land.py` and record migration evidence.

**Risks.** Active sessions may hold board paths; some other project boards are untracked or dirty. Never overwrite or silently move a board while a live writer is using it. A hidden folder is omitted by plain `rg`, so generated agent instructions must name it and teach explicit searches. `issues/` is a guarded legacy folder and requires a deliberate migration of this repository's own references.

**Verify.** Python board/project tests; C++ projects/boardworkspace/projectinit/boardmodel tests; isolated board creation and rename; per-project before/after card counts and `board.yaml`/policy checks; `relay-board.py check` on migrated boards.

## Tasks

- [x] Make .board the default in Relay setup and discovery; preserve old folder support <!-- t:jf -->
- [x] Build and run focused tests for setup, discovery, and rename <!-- t:sc -->
- [x] Back up and migrate other real local project boards <!-- t:6s -->
- [x] Coordinate live sessions, then migrate this repository's issues board last <!-- t:8a -->
- [x] Verify all board paths, document evidence, and land the migration record <!-- t:9h -->
