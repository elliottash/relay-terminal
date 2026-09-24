# #QJXD — staging notes

Staged by the implementing session on 2026-09-24. `stage.sh` in the same folder stages the whole situation; `stop.sh` ends the staged instance.

## What is staged, and why

- **`fixture/`** — a throwaway git project with a Relay board holding exactly one card, `#AA01`, committed before the app starts. The pane's card index therefore loads non-empty at startup: the "stale index" case, not the empty-index case.
- **`fixture/.board/changes/.b7xz.md` → `2026-09-24-arrives-later.md`** — card `#B7XZ` exists only as a hidden dot-file in the board folder, invisible to the board scanner, until `stage.sh` copies it into place **while the app is running**, 15 s after the pane opened. That copy is the "session wrote a card straight to disk" moment; no board tool is involved.
- **Sandboxed `$HOME/.bashrc`** — the pane's shell prints `tracked in #AA01 and waiting on #B7XZ` as it starts, so the line under test is in the pane's output without typing into any terminal pane.
- **Screenshots** — `docs/qa_evidence/2026-09-24-qjxd-card-links/tryit/01-before-card-on-disk.png` (both ids plain until hovered; `#AA01` is in the index, `#B7XZ` is not) and `02-after-card-on-disk.png` (8 s after the card landed — one full 5 s unknown-card gate window later).
- The app is **left running** on its Xvfb display (`tryit/status.md` names it) so the hover itself can be played on the staged instance; `stop.sh` ends it.

## What the mechanical pass cannot do

Underlining a scanned card id happens on hover or a keyboard link walk (`TerminalView::updateHover`), and neither mouse coordinates nor terminal input are drivable by name. The before/after screenshots and the two unit tests carry the scan logic; the hover-and-click judgement is the owner's single task.

## Re-staging

`stage.sh` is idempotent: it stops any staged instance, rebuilds the fixture, and replays the sequence. It needs the repo's `build/relay` (commit `aab23d78f3ed`) and ImageMagick's `import`.
