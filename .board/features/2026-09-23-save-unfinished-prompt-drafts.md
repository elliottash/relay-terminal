---
id: Z8DR
type: work
status: needs-verification
labels: [feature, composer]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [ad7213d3825b6dae5b23e864797817e4fb3a5ef1], evidence: [docs/qa_evidence/2026-09-23-save-drafts/], related: [H8VP], github: null}
---
# Save unfinished prompt drafts on exit and crash

## Issue
is it possible on exist / crash, the text that is already typed in any prompt boxes is also saved

## Done means
- Text left in each terminal pane prompt box returns in that pane after a normal quit or crash.
- Text left in prompt boxes embedded in Board, Options, Actions, Sessions, and Models returns in the correct surface after a restart.
- Submitting or clearing a prompt removes its saved draft; a draft is not added to submitted prompt history.
- Draft persistence does not restore secrets typed in a masked password field.

## Plan
**Goal:** Restore unsent text in each prompt box after restart.

**Findings:** `src/WindowManagerImpl.h` saves layout on changes, while `src/RelayWindow.h` omits composer text from pane nodes; hosted consoles use `src/Pane.h` and card replies also keep per-card text in `src/BoardPane.cpp`.

**Steps:** 1. Add an atomic draft store keyed by pane or hosted-console identity. 2. Connect composer edits and restoration to it, including per-card replies. 3. Verify normal quit, forced termination, and clearing or submitting.

**Risks:** Each edit writes a small file synchronously. A crash during an edit leaves the previous complete draft. Restore only into the same pane or context.

**Verify:** Targeted C++ tests plus an isolated GUI restart drive.

## Execution Summary
Unsent text now writes atomically to a private draft file on every composer edit. Terminal panes use their saved pane ID; hosted consoles use the tab and prompt surface, with card replies keyed by card. A fresh window set removes the draft store. The isolated live drive restored terminal, Board, and card reply drafts after `SIGKILL`.

![Terminal draft restored after SIGKILL](docs/qa_evidence/2026-09-23-save-drafts/restart.png)
![Board prompt restored after SIGKILL](docs/qa_evidence/2026-09-23-save-drafts/board-restart.png)
![Card reply restored after SIGKILL](docs/qa_evidence/2026-09-23-save-drafts/card-restart.png)

## Tests
- `scripts/relay-build --target relay`
- `scripts/relay-build --target relay-windowstate-tests`
- `ctest --test-dir build -R '^windowstate$' --output-on-failure`
- Manual: `docs/qa_evidence/2026-09-23-save-drafts/drive.log` and the three restart screenshots, using an isolated Xvfb profile. Clearing with Ctrl+A, Delete removed the card reply draft and it remained empty after restart.
