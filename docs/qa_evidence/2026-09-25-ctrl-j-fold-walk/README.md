# Ctrl+J: step through tool calls and reasoning (card #XPEB)

Implementer evidence, 2026-09-26. `drive.sh` runs `build/relay` under Xvfb with an isolated HOME
and a loopback stub provider (`stub-provider.py`, copied from #QT8C): one turn of reasoning, a
`run_command` and two `read_file` calls merged into one row. OCR of every shot is in
`implementer-notes.txt`.

| Shot | Keys | What it shows |
|---|---|---|
| `01-turn-done.png` | — | the turn at rest: `✦ thought`, `ran sleep`, `read 2 files`, `✦ 3 tool calls` |
| `02-ctrl-j.png` | Ctrl+J | the walk starts on the newest line, `✦ 3 tool calls` (4 of 4) |
| `03-up.png` | Up | `read 2 files` (3 of 4), toast says Enter or → unfolds |
| `04-enter-unfolds.png` | Enter | its fold opens in place; the walk stays on the line |
| `05-up-right.png` | Up, Right | `ran sleep` unfolds; the line stays on screen after the worker's detail arrives |
| `06-up-thinking.png` | Up | the reasoning line (1 of 4), scrolled back into view above two open folds |
| `07-shift-enter.png` | Shift+Enter | every fold of the turn open, the reasoning included |
| `08-esc.png` | Esc | the walk is over: no highlight, no toast |
| `09-ctrl-j-esc.png` | Ctrl+J, Esc | a fresh walk left at once |

Esc in an empty prompt box used to arm Esc Esc (Rewind), and during a turn stop the agent, before
the window saw it, so it never ended a walk — the Ctrl+L link walk included. `Pane::handleComposerKey`
now leaves Enter, Esc and the arrows to the window while either walk runs.
