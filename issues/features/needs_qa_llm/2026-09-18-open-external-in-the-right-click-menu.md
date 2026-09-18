---
id: V9V1
type: work
status: needs-qa-llm
labels: [feature]
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-18
rank: zzzz108
created: '2026-09-18'
acceptance: "every file's context menu reads: open internal, open external, open folder"
source: 'issues/feature_intake.txt, 2026-09-18'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-intake-fixes/'], related: [S1JP], github: null}
---
# An open-external button for every file, and a menu order to match

## Request
need an open external button for all files, when you right click it should say open internal at the top and open external second and open folder third

## What "open external" is

`QDesktopServices::openUrl()` — the desktop's default application. `scripts/relay-open` and
`RELAY_OPEN_HELPER` go the other way (they carry a path from a shell or a `relay://` link **into**
Relay, `ARCHITECTURE.md` §10), so nothing new was added for them and nothing was taken from them.
The explorer already had an "Open" entry that did exactly `openUrl`; it just did not say so, and it
sat above the entry that opened the file inside Relay.

## Behavior as implemented

Both menus lead with the owner's three, in the owner's order (`relay::openEntries()`, shared so the
two menus cannot drift apart):

| | Explorer, a file | Explorer, a folder | Preview pane |
|---|---|---|---|
| **Open internal** | a preview pane | becomes this explorer's root | reopens the file here (which also returns a Markdown file to the rendered view) |
| **Open external** | the desktop's default application | the desktop's file manager, on that folder | the desktop's default application |
| **Open folder** | the file selected in its folder in the desktop's file manager (`FileManager1.ShowItems`, falling back to opening the folder) | the same for the folder | the same |

- The old **"Open"** (external, top) and **"Open in a preview pane"** entries are gone, as is
  **"Reveal in file manager"**, which is what "Open folder" now is. The ids changed with them:
  `open`/`preview`/`reveal` → `openInternal`/`openExternal`/`openFolder`.
- "Open internal" is greyed out, never dropped, when a host has not wired up the preview — the
  order is a promise the menu keeps even when an entry cannot run.
- The empty space below the explorer's rows offers "Open external" and "Open folder" for the folder
  it is showing; there is no "Open internal", because that folder is already open there.
- **A preview pane had no menu of its own at all** — a right-click got the QTextEdit menu. It now
  has the three entries plus Copy path, and the viewer's own Copy / Select all / Copy link location
  follow under a separator, so nothing was lost. It is an event filter on the viewports: QTextEdit
  answers ContextMenu inside `viewportEvent()`, before a context-menu policy is consulted.

## Implementer check (not a QA verdict)

`tests/filepanes_test.cpp` (`everyMenuLeadsWithTheThreeOpenEntries`,
`openInternalIsGreyedOutForAFileWithNoPreviewHost`, `previewOffersTheMenuOnceAFileIsOpen`, plus the
existing menu tests updated) and `ctest -j16` green. Xvfb :190:
`docs/qa_evidence/2026-09-18-intake-fixes/` (`v9v1-…`). Under Xvfb there is no desktop handler, so
"Open external" and "Open folder" were read from the code, not watched opening an application —
that is for human QA.

## QA checklist

1. Right-click a file in the explorer: the first three entries are **Open internal**, **Open
   external**, **Open folder**, in that order, then Navigate here, the two copies, and the
   create/rename/delete group.
2. Right-click a folder there: the same three at the top.
3. Right-click the empty space below the rows: Open external, Open folder, Navigate here, New
   file…, New folder….
4. "Open internal" on a `.md` file opens a rendered preview pane; on a folder it moves the explorer
   into it.
5. "Open external" on a `.png` opens the system image viewer; on a `.md` the system Markdown or
   text handler; on a folder, the file manager showing that folder.
6. "Open folder" on a file opens the file manager with that file selected.
7. Right-click **inside a preview pane** (a Markdown one, a text one, an image, and a binary file's
   info page): the same three entries, then Copy path, then the viewer's own Copy / Select all.
8. In the preview's menu, Copy still copies the selected text and Copy path copies the file's path.
9. Right-click over a link in a rendered Markdown preview: "Copy link location" is there, and the
   three entries still act on the file the pane is showing, not on the link.
10. In a read-only folder, the three entries still work; only New/Rename/Delete are greyed out.

Implementer evidence: docs/qa_evidence/2026-09-18-intake-fixes/
