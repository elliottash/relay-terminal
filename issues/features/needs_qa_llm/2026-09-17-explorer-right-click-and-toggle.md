---
id: D60R
type: work
status: needs-qa-llm
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
rank: zzd6
created: '2026-09-17'
acceptance: right-click offers the listed actions, Navigate here moves the terminal, and one shortcut opens and closes the explorer
source: '`issues/feature_intake.txt`, 2026-09-17: explorer right click, navigate here, folder click closes it, shortcut to toggle'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-ux-batch2/'], related: [0C7V], github: null}
---
# File explorer: right-click menu, navigate here, and a toggle shortcut

- Right-click menu in the explorer pane: **Navigate here** (sets the terminal's directory), open, open in a
  preview pane, copy path, copy relative path, reveal in the system file manager, new file/folder, rename,
  delete (with confirmation), and "Set as agent workspace".
- Decide whether Ctrl+click also navigates (proposal: no — single click opens, Ctrl+click adds to selection).
- Clicking the project folder in the header closes the explorer when it is already open.
- A shortcut that opens **and closes** the explorer (proposal: Ctrl+Shift+E, free in all presets).

## Decisions

- **Ctrl+click does not navigate and does not open.** The explorer's view is now
  `QAbstractItemView::ExtendedSelection`, and Ctrl+click and Shift+click extend the selection, exactly as the
  card proposed. With single-click opening on (card #0C7V) a plain click opens and a modified click selects,
  which is Dolphin's split and the one people already know. A drag never opens either, because Qt only emits
  `clicked()` when press and release land on the same row.
- **The toggle key is Ctrl+B, with Ctrl+Shift+B as its twin**, not Ctrl+Shift+E. Ctrl+E and Ctrl+Shift+E went
  to "new pane" while this card was being built (commit 9191fa8, owner's one-handed key). Ctrl+B is VS Code's
  sidebar key, so it is the obvious one for "show/hide the file tree", and it is free: no Relay default and
  none of the four preset tables (`relay`, `warp`, `vscode`, `konsole`) binds Ctrl+B or Ctrl+Shift+B — checked
  by grepping the whole keymap block, which holds both the defaults and `presetJson()`. Ctrl+Shift+B is the
  twin that a program cannot swallow, following the pattern of card #5FY5.
- **"Open" is not the same as "Open in a preview pane."** For a folder, Open walks into it. For a file, Open
  hands it to the desktop (`QDesktopServices::openUrl`, so the system's default application), and "Open in a
  preview pane" is the Relay preview. Both are offered for a file so the menu can do either.
- **"Set as agent workspace" asks first**, because re-configuring the worker starts a new conversation in that
  pane. It is offered for a folder and for the empty space (which means the folder being shown), not for a file.

## Implemented

### The menu (`src/FilePanes.h`, `src/FilePanes.cpp`)

`relay::explorerMenu(FileMenuTarget, FileMenuHost)` returns the entry list as data — ids, labels, enabled
flags and `"-"` separators — so which entries appear for a folder, for a file and for the empty space can be
checked without a window. `FileExplorer` turns it into a QMenu on `customContextMenuRequested`, and the Menu
key and Shift+F10 open the same menu on the current row.

- A **folder**: Open · Navigate here — Copy path · Copy relative path — Reveal in file manager — New file… ·
  New folder… · Rename… · Delete… — Set as agent workspace.
- A **file**: the same, plus **Open in a preview pane**, and without "Set as agent workspace".
- The **empty space** below the rows acts on the folder being shown: Navigate here — Reveal in file manager —
  New file… · New folder… — Set as agent workspace. Copy path, Rename and Delete are left out, because
  nothing was clicked.
- An entry the host has not wired up is left out, not shown dead. The four write entries are greyed when the
  containing folder is not writable.
- Copy path and Copy relative path (relative to the explorer's root) go to the clipboard. Reveal asks
  `org.freedesktop.FileManager1.ShowItems` over D-Bus and falls back to opening the containing folder.
  New file / New folder / Rename take a name in a `QInputDialog`, refuse a name containing `/` or one that
  already exists, and select the result. Delete warns first — naming the file or folder, and saying it cannot
  be undone — and defaults to Cancel; a folder goes with `QDir::removeRecursively`.

### Navigate here, preview and workspace (`src/main.cpp`)

`FileExplorer` gained four callbacks that the window fills in `createToolPane`:
`onNavigateHere`, `onOpenInPreview`, `onSetWorkspace` and `onCloseRequested`.

- **Navigate here** finds the terminal pane the menu should act on — the one last active in the same tab, else
  the first one there — and runs a quoted `cd` in it through the pane's normal command path, so the shell and
  its prompt follow. For a file it uses the folder the file sits in.
- **Set as agent workspace** confirms first, then `Pane::setAgentWorkspace()` sets the pane's workspace and
  re-sends `configure` with the current preset, so the agent's file tools are restricted to that folder.

### One path opens and closes the explorer

`RelayWindow::toggleExplorer(path, anchor)`: an explorer pane in this tab already showing that folder is
closed (recorded, so Ctrl+Shift+Z brings it back); one showing another folder moves to the folder; otherwise a
new explorer pane opens. Three things come here — the `files.explorer` action (**Ctrl+B** / **Ctrl+Shift+B**),
the folder line in a terminal pane's header, and the folder line in the explorer's own header. The terminal
pane's folder line shows a shortcut hint the first few times it is used, per WARP.md.

### Tests

`tests/filepanes_test.cpp` (`filepanes` group, no new ctest group): the folder / file / empty-space lists,
that the preview entry is files-only and the workspace entry is not, that unwired entries drop out, that a
read-only folder greys exactly the four write entries, that no menu ever starts, ends or doubles a separator
and every entry has a label (every combination of the four host flags), and that `menuFor()` reads the
explorer's own root when nothing was clicked.

### Evidence

`docs/qa_evidence/2026-09-17-ux-batch2/`, both engines (`implementer-relay-*`, `implementer-konsole-*`):
`02-explorer-open`, `03-explorer-menu-folder`, `04-explorer-menu-file`, `05-explorer-menu-empty`,
`07-ctrl-click-selects`, `08-explorer-closed`.

Build: `cmake --build build` with no new warnings. `./scripts/test.sh` 508 passed; `ctest --test-dir build`
16/16.

## QA checklist

1. Press **Ctrl+B** with the keyboard in the prompt box: an explorer pane opens on that pane's directory.
   Press Ctrl+B again: it closes. Repeat with **Ctrl+Shift+B**, and from inside a running program (`vim`) —
   Ctrl+Shift+B must still work there, Ctrl+B may not.
2. Click the folder line in a terminal pane's header: the explorer opens. Click it again: it closes. Now open
   the explorer, `cd` somewhere else in that pane, and click the folder line: the explorer must move to the
   new folder rather than close.
3. Click the folder line in the **explorer's own** header: the explorer closes. Press Ctrl+Shift+Z: it comes
   back.
4. Right-click a folder, a file, and the empty space below the rows. Check each list against "Implemented"
   above.
5. **Navigate here** on a folder: the terminal pane's shell changes into it and its prompt and header follow.
   Try it on a file (must use the file's folder) and from the empty space. Try it while a command is running
   in that pane — it should queue or report, not lose the directory.
6. **Open** on a folder walks into it; **Open** on a file hands it to the desktop's default application;
   **Open in a preview pane** opens Relay's preview.
7. **Copy path** and **Copy relative path**: paste both into the prompt box and check them, including for a
   file in a subfolder and after navigating up.
8. **Reveal in file manager** on the owner's real desktop: Dolphin (or the default file manager) opens with
   the entry selected.
9. **New file…**, **New folder…**, **Rename…**: try an empty name, a name with `/`, and a name that already
   exists — each must refuse with a message and change nothing. A good name must appear in the list and be
   selected.
10. **Delete…** on a file and on a non-empty folder: the warning must name it and say it cannot be undone, and
    Cancel (the default) must change nothing. Confirm once and check the entry is gone from disk.
11. Right-click inside a folder you cannot write to (e.g. `/usr`): New file, New folder, Rename and Delete
    must be greyed.
12. **Set as agent workspace** on a folder and on the empty space: the confirmation must name the folder, and
    Cancel must change nothing. After confirming, ask the agent in that pane to read a file above the new
    workspace — it must refuse — and one inside it — it must succeed.
13. **Ctrl+click** and **Shift+click** rows: the selection extends and *nothing opens*. Drag across rows: a
    selection, not an open.
14. The keyboard paths still work: type-ahead filtering, ↓ from the filter, Enter to open, Backspace / Alt+↑
    for the parent folder, and the Menu key (or Shift+F10) for this menu.
15. Repeat 1, 4, 5 and 12 with a KonsolePart pane (`--engine=konsole`).
