---
id: H7N4
type: work
status: needs-qa-llm
labels: [change, bug, gui]
component: [gui]
assignee: agent
implemented_by: GLM-5.3 (Relay), 2026-09-19
rank: zzzzj
created: '2026-09-19'
source: pane 1, 2026-09-18
links: {evidence: [docs/qa_evidence/2026-09-19-pane-prompt-histories/], related: [H8VP], github: null, commits: [], plans: []}
---
# Up/Down in the prompt box walks this pane's own history, one file per pane

## Issue
the up/down history seems to be getting commands from other panes, not just mine

## Report
One shared history file served every pane (#H8VP), so Up in a pane recalled lines typed at any
other pane, and a new pane opened with the whole merged history in it (owner, 2026-09-19: "the
up/down history seems to be getting commands from other panes, not just mine" — "i want pane
histories for up/down").

## Change

One file per pane, `$XDG_DATA_HOME/relay/state/prompt-history/<id>.txt` (0600, 0700 tree), keyed
by the same layout id that already names the pane's saved scrollback (`src/WindowState.h`) — the
id a pane keeps through a restart and through "restore last closed".

- `relay::prompthistory` (`src/PromptHistory.h/.cpp`) re-shapes around it: `pathFor(paneId)`, a
  per-directory `prune(keepIds)` and `clearDirectory()`, and `clearAll()`, which also removes the
  pre-2026-09-19 shared `prompt-history.txt`. The old shared file is not migrated: its lines
  cannot be attributed to a pane, so every pane starts its own file empty.
- `Pane` points its composer at its own file (`promptHistoryPath()`, buildUi) and re-points it at
  the restored id in `initRestore()`, so a pane restarted or reopened picks its history back up.
  A prompt from a paired phone is written to the pane it was sent to (`submitRemote`), and is
  recalled there.
- The ghost-text history half follows on its own: it reads the composer's in-memory history.
- Pruning runs with the scrollback prune in `WindowManager::writeWindows()` — same id list, from
  the saved layout and the recently-closed list — and "Start a fresh window set" drops the store
  with the layout (`clearAll()`).
- "Clear prompt history" clears the directory and calls the new `RichEditor::forgetAllHistory()`
  (every open box, whatever file it is on, including one mid-browse).
- Docs: `docs/ARCHITECTURE.md` rewritten for the section; `src/WindowState.h`'s node-shape note
  says the `scrollback` id names both stores.

## QA checklist

- [x] `ctest -R "prompthistory|editor"` — 2/2, with new cases: per-pane paths, prune,
      `clearDirectory`, separate histories for two boxes, `forgetAllHistory` mid-browse
      (`docs/qa_evidence/2026-09-19-pane-prompt-histories/ctest.log`).
- [x] Whole app builds with the change (`relay` target links).
- [x] Full `ctest`: all C++ groups pass; the four `backend-and-bash` failures are in Python
      modules with uncommitted edits from other in-flight work, untouched by this change.
- [ ] Live GUI run: two panes keep separate Up/Down histories; quit and reopen recalls each
      pane's own lines; a phone prompt is recalled in the pane it was sent to.
- [ ] Not committed: the tree is shared with other in-flight work that has staged files
      (including these); committing here would sweep that work in. The change sits in the working
      tree and index, built and tested.
