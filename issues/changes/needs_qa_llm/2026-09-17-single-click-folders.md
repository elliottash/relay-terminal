---
id: 0C7V
type: work
status: needs-qa-llm
labels: [bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
rank: zzw
created: '2026-09-17'
acceptance: a single click opens a folder in the explorer pane by default, and the Settings window offers double click instead
source: '`issues/feature_intake.txt`, 2026-09-17: "use single click (dolphin style) folder opening by default, you can change that in the options menu."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-ux-batch2/'], related: [D60R], github: null}
---
# Single click opens folders in the explorer pane (Dolphin style)

Default to single click for folders and files in the explorer pane, with a setting in the Settings window
("Open items with a single click", on by default). Selection still works (drag, Ctrl+click, arrow keys), and
the setting should also be respected by the file picker used for previews.

## Implemented

### The explorer (`src/FilePanes.h`, `src/FilePanes.cpp`)

`FileExplorer` has `setSingleClick(bool)` / `singleClick()`, and a new explorer starts from
`FileExplorer::singleClickDefault()` — the `files/single_click` setting, **true when it has never been set**.

Opening hangs off `QTreeView::clicked`, which Qt only emits when the press and the release land on the same
row without a drag, so a drag still selects. The modifiers are taken from the press (recorded in the
viewport's event filter, not read back later), so **Ctrl+click and Shift+click never open** — they extend the
selection, now that the view is `ExtendedSelection` (card #D60R). `doubleClicked` still opens, and returns
early while single click is on so one gesture cannot open twice. Enter, the filter box's Enter, Backspace and
Alt+↑ are untouched.

### The setting (`src/main.cpp`)

Settings › **General**, between "Recap when you come back" and "Reopen windows on start":

> **Open items with a single click** — In the file explorer and the file picker; Ctrl+click and Shift+click
> still select

It sits in General rather than Terminal because Terminal is "the shell side of a pane" and this is about the
file panes. It carries search aliases (`dolphin explorer double click files folders`), so the palette finds it
by either name. Turning it off or on reaches **every explorer pane already open, in every window**
(`RelayWindow::applySingleClickSetting`), not just the next one.

### The preview file picker

"Open file…" (`files.open`) used `QFileDialog::getOpenFileName`. Qt's dialog has no single-click option, so
`RelayWindow::pickFileForPreview()` now builds the dialog itself: with the setting off it behaves exactly as
before, and with it on it uses the non-native dialog and connects each of its item views' `clicked` signals —
a click walks into a folder or picks a file and closes, unless Ctrl or Shift is held. `@`-picker behaviour is
unchanged; it was already single click.

### Tests

`tests/filepanes_test.cpp` (`filepanes` group): single click is on when the setting has never been written,
`setSingleClick` follows, writing the setting false makes a newly created explorer start double-click, and the
test puts the setting back.

### Evidence

`docs/qa_evidence/2026-09-17-ux-batch2/`, both engines: `06-single-click-folder` (one click walked into
`alpha`), `07-ctrl-click-selects` (Ctrl+click selected and opened nothing), `12-settings-single-click` (the
Settings row, on).

Build: `cmake --build build` with no new warnings. `./scripts/test.sh` 508 passed; `ctest --test-dir build`
16/16.

## QA checklist

1. Open an explorer pane (Ctrl+B). A **single** click on a folder walks into it; a single click on a file
   opens it in a preview pane. Nothing needs a double click.
2. Double-click a folder with the setting on: it must open **once**, not twice, and not open a child.
3. **Ctrl+click** and **Shift+click** several rows: the selection grows and nothing opens.
4. Press on a row and drag across several rows before releasing: a selection, and nothing opens.
5. Arrow keys move the current row without opening; Enter opens. Type a few letters to filter, ↓ into the
   list, Enter — unchanged.
6. Settings (Ctrl+,) › General: **Open items with a single click** is on. Turn it off **with an explorer pane
   already open**: that pane must switch to double click at once, and in a second window too. Turn it back on.
7. With the setting off: a single click only selects; a double click opens.
8. Actions palette › "Open file…" with the setting **on**: one click on a folder in the dialog walks into it,
   one click on a file picks it and the preview pane opens. Ctrl+click inside the dialog must not pick.
9. The same with the setting **off**: the dialog is the ordinary one — double click, or select and Open.
10. Restart Relay and check the setting stuck, then check a restored explorer pane (from the saved layout)
    follows it too.
11. Repeat 1, 3 and 6 with a KonsolePart pane (`--engine=konsole`) — the explorer is shared, so it should be
    identical.
