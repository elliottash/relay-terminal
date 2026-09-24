---
id: H7KP
type: work
status: needs-qa-llm
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'The three pane buttons are on screen in every pane at all times; pressing anywhere on a pane header and dragging moves the pane onto another pane''s edge or onto the tab bar; a click on the header still renames and still opens the folder; `ctest` passes'
source: 'owner, 2026-09-18: "i dont like that the top-right buttons on the panes only show up when the pane is active... make panes draggable by clicking and dragging anywhere on the header. and you can drag them into other panes or a new pane as well"'
links: {plans: [], commits: [f099fc1], evidence: ['docs/qa_evidence/2026-09-18-pane-buttons-and-header-drag/'], related: [0T2R], github: null}
---
# Pane buttons that are always there, and a header you can drag

## Report

The pane's button row appeared only under the mouse, so the three buttons a person reaches for
had to be gone looking for, and moving a pane meant finding the ⠿ grip inside that hover row.

## Change

- **The three always-on buttons.** New pane (`◫+`), move to a tab of its own (`⇱`) and close
  (`×`) are drawn in every pane at all times, quiet, on the pane's own ground. Pointing at a pane
  lifts its row onto the raised tile and adds the two it hides: the drag grip and "new pane
  below". Both hover-only buttons sit to the *left* of the always-on three, because the row is
  right-anchored and anything else moves a button out from under the cursor that opened it.
- **The header is a handle.** `Pane::headerDragEvent` watches (never takes) a press anywhere on
  the header — title, "auto" badge, folder line, the gaps — and once it passes the platform drag
  distance the whole pane travels. Drop on another pane's edge to split it, on the tab bar for a
  tab of its own, Esc to put it back. Under the threshold it is still a click, so double click
  still renames.
- **Tool panes too.** `RelayWindow::toolHeaderDrag` gives the explorer, previews, plans and the
  Switchboard the same gesture generically: a press in the pane's top strip on something that is
  not a control (buttons, filter boxes, lists, the Switchboard's tabs and the pane chrome are all
  excluded).
- The header reserves the full row's width permanently, so the title and the folder line do not
  re-elide as the mouse comes and goes.

Two consequences worth knowing: the folder line is no longer selectable text (dragging across it
moves the pane; its tooltip still holds both paths in full), and it opens the explorer from the
drag handler now, because the press decides which widget is offered the release and that is no
longer the label.

## QA checklist

1. **At rest.** Every pane shows exactly three buttons with no tile behind them, including panes
   that are not focused and panes in another tab.
2. **On hover.** Pointing at a pane adds the grip and "new pane below" and raises the tile; the
   three original buttons do not move, and the header text does not re-elide.
3. **Drag between panes.** With two panes open, press the right pane's title and drag to the left
   pane's left edge: a drop zone covers that half, and the release puts the pane there.
4. **Drag to the tab bar.** The same drag onto the tab bar highlights it and the release gives the
   pane its own tab, with its shell and conversation intact.
5. **Esc.** Start a drag, press Esc: nothing moves and the cursor returns to normal.
6. **Clicks still work.** Double click a title → rename box. Single click the folder line → the
   explorer opens, and again → it closes.
7. **Tool panes.** Drag an explorer pane by its header; the buttons inside its header (↑, the
   filter box, the file list) still do what they did and do not start a drag.
8. **Three-pane row.** Showing and hiding the hover extras must not resize sibling panes.

## Known gaps

- The resting buttons are dimmer than the old hover row; the owner has asked for the brighter
  outline back (card 0T2R, still open).
- The gestures are documented on the Actions tab (`info:mouse`) but the settings pane's search
  skips informational rows, so a search for "drag" does not find them.
