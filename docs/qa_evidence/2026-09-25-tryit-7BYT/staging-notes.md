# #7BYT Try-it staging notes

Staged 2026-09-25. `stage.sh` (rerunnable, no model, no network):

- Exports the landed commit `1cc5b1d` (not the shared checkout, whose working tree holds another
  session's in-flight `BoardPane.h`) into `~/.cache/relay/scratch/tryit/7byt/src` and builds
  `relay` there through the shared ccache — ~7 s warm, a few minutes cold.
- Seeds `…/7byt/proj`: `src/boom.py` (NameError on line 3), `src/` with files, `README.md`,
  `notes/`. `python3 src/boom.py && ls src` fills the pane with `path:line` links and file links.
- Prints the open line: `~/.cache/relay/scratch/tryit/7byt/build/relay --workspace
  ~/.cache/relay/scratch/tryit/7byt/proj --clean-shell --fresh`.

## The AI's mechanical pass

`ai-pass.sh` + `drag-pass.sh` drive the same binary under Xvfb on an isolated profile. Keyboard
chords go through the real link walk (`Ctrl+Shift+L`, then `Alt+Enter` / `Ctrl+Enter`); the
Alt+drag is a real pointer drag. One screenshot per step; OCR of the shots shows:

- `10-output.png` — the traceback and `ls src`, links present.
- `20-walk.png` — the walk highlighting the first link.
- `30-mention.png` — after Alt+Enter: toast `Added to this prompt: @src/boom…` (the mention is
  `@src/boom.py:3 `).
- `40-navigated.png` — after Ctrl+Enter on the next link: the shell prompt is now
  `…/proj/src$` and the pane title shows `src` (the `cd` the old Alt+click used to do).
- `50-selection.png` — after Alt+drag over the plain prompt rows: a second `Added to this
  prompt:` toast, the composer in agent mode (model picker visible).

What the pass does not cover (left to the person, on purpose): mouse Alt+click directly on an
underlined link, the right-click menu entries, and whether the chord mapping feels right.

## Not simulated

No Board fixture: `#ID` card links share the same `addLinkToContext` path as the file link (the
card menu's `Add to prompt` entry calls it), so the card case is one menu click away in any real
project with a board.
