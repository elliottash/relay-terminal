# #QG60 — Xvfb GUI run: shortcuts in parentheses, live on screen

Date: 2026-09-19 · implementer: `claude-opus-4-5` · `drive.sh` in this directory.

Relay run under Xvfb with an isolated HOME/XDG dirs (harness copied from the #HQ2B
relaying-line run, including its stub provider so no provider-setup dialog appears — no
model is called). The workspace is a sandbox snapshot of this repo's own `issues/` board
(289 open cards), so the pane is a real Switchboard. `Ctrl+Shift+S` opens the board as a
split beside the terminal: a 1900 px window leaves the board pane ~450 px wide, the
default 1440 px window leaves it ~350 px — the tight case the card asks about.

The binary is the shared `build/relay`, which at run time also carried another card's
in-flight work (#T71W cross-provider QA: a fifth reply button `Verify (v)` and a verify
line on QA-lane cards). My commit `38836d8` contains only the fourteen label lines.

## Frames

- `list.png` / `list-2x.png` — the board list page. OCR, with 2× coordinates:
  `+ New card (n)` at (3350..3544, 268) and `Clean up` at (3624..3738, 268) in the tools
  row; `2 shown` count and the `Filter — any word in the card, label:bug, …` placeholder
  after typing `parentheses`.
- `card.png` / `card-2x.png` — a card page in the ~450 px pane (a Needs QA (LLM) card,
  #M9T4): `+ Back to board (Est` — OCR's reading of `←  Back to board (Esc)` — the
  `#ID` chip, the meta line, and the reply placeholder `Reply — Enter d[iscusses…]`.
- `narrow.png` / `narrow-2x.png` — a card page in the ~350 px pane. OCR (6× crop of the
  reply frame): `Plan (p) | Execute (x) || Verify (v)` on the row's second line, and
  above it the first line's button boxes (Comment/Discuss) drawn but below OCR contrast
  in the dark theme; header row `© Edit (e) | … Open file (o) …` fits.

## What this establishes

- The suffixed labels render live: `+  New card (n)`, `←  Back to board (Esc)`,
  `Edit (e)`, `Open file (o)`, `Plan (p)`, `Execute (x)` (and, from the other card's
  work riding in the binary, `Verify (v)` follows the same convention).
- At ~350 px the reply row wraps instead of overflowing: Comment/Discuss on one line,
  Plan/Execute/Verify on the next, all inside the pane. With five buttons the row
  already overflowed a ~350 px pane before the suffixes; the parentheses add ~90 px, and
  the wrap absorbs it.
- `Comment (Ctrl+Shift+Enter)` and `Discuss (Enter)` are on the wrapped first line of
  that row (boxes visible in the frame); their exact text is asserted by the board unit
  test (`aCardOffersDiscussPlanAndExecuteAndTheThreadNamesTheMode`), which passes.

## Not covered by this run

`#ID → prompt (t)`, `Cancel (Esc)`, `Save (Ctrl+Enter)` and `Undo (Ctrl+Z)` were not
separately screen-shot (they need the header's right side legible, an edit frame, and a
notice — none triggered); the board unit test and the code cover them. `./scripts/test.sh`
and a clean full `ctest` run were skipped at the owner's request this session.

Logs from the run are in `logs/`.
