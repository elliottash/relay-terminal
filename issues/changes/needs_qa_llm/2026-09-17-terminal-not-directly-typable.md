# The prompt box is the only input; clicking the terminal does not type into it

- **Status**: needs-qa-llm
- **Component**: gui
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: `docs/qa_evidence/2026-09-17-prompt-box-only-input/` (implementer run, both engines); a non-Claude model QA session runs the checklist below and records it there
- **Assignee**: implemented by Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
- **Source**: `issues/feature_intake.txt`, 2026-09-17: "you shouldnt be able to click into the terminal and type there. it should be like warp where the prompt box is the input."

## Today

The terminal widget takes focus on click and passes keys straight to the shell. Relay also switches to
native input by itself for full-screen programs (vim, htop), ssh sessions and password prompts, and
Ctrl+H hands the keyboard over on purpose.

## Proposed

- Clicking the terminal selects text, scrolls and follows links, but never takes the keyboard; the prompt
  box keeps focus and typed keys go there.
- Keep the deliberate exceptions: Ctrl+H (take control), full-screen programs, ssh/mosh, password prompts
  and the queued-command flow, each of which already shows a banner saying who has the keyboard.
- Keys that must still reach a running program while the prompt box has focus (Ctrl+C, Ctrl+D, Ctrl+Z,
  arrow keys for a pager) need a rule: either they are forwarded from the prompt box when a program is
  running, or the user takes control first.

## Open questions
1. When a program is running and the prompt box has focus, should keystrokes be forwarded to it, or should
   the box always queue the next command (recommended, with Esc to interrupt and Ctrl+H to take over)?
2. Should double-click-to-focus or a small "type here" affordance exist for people who expect a normal
   terminal, or is the banner enough?

## Decisions (owner, 2026-09-17)

- **The prompt box is always the input.** Clicking the terminal selects text; it never takes the keyboard.
- **Password prompts**: when Relay detects one (ICANON on, ECHO off), the prompt box switches to masked
  input (asterisks) and the line is submitted to the running program instead of the shell. Security rules:
  the text never enters prompt history, the request ledger, the session file, logs, route assist or any
  model prompt, and is cleared from memory after it is written to the program.
- **Ctrl+I still switches to agent mode** at such a prompt, so the user can say e.g. "paste the password
  from my clipboard"; the agent's typing into the program follows the normal control rules.
- **Full-screen programs** (vim, htop, ssh): instead of switching by itself, Relay shows a small
  **"Take control (Ctrl+H)"** button; the prompt box stays until the user presses it.

## Open question (answered by the owner: the recommended behavior)
- While an ordinary (non-full-screen) program is running and it is reading stdin (e.g. a `[Y/n]` prompt),
  should a submitted line go to that program automatically (recommended, consistent with the password case)
  or still be queued as the next shell command?
- **Answer:** it goes to the program, with a short "Sent to <program>" status. The exact rule as
  implemented is in the Implemented section, point 5.

## Implemented (2026-09-17)

Rules live in the Pane, not in a backend, so KonsolePart panes and Relay-engine panes behave the
same. The decisions themselves are pure functions in `src/InputPolicy.{h,cpp}` (static library
`relay-input`, ctest group `input`, `tests/inputpolicy_test.cpp`, 13 cases).

1. **Clicking the terminal never takes the keyboard.** The backend's focus widget is set to
   `Qt::NoFocus` while the composer is shown (restored in native mode), and a `FocusIn` on the
   terminal surface hands the focus straight back to the prompt box; widgets inside the terminal
   that are meant to be typed in (the Relay engine's Find bar) keep it. `TerminalView` no longer
   grabs the focus on a mouse press when its host gave it `Qt::NoFocus`. Selection, scrolling,
   link clicks, copy-on-select and Ctrl+C/Ctrl+V in the terminal are unchanged. `Pane::focusTerminal`
   is a no-op outside native mode, so no internal path can move the keyboard there any more. A
   plain click shows the hint "The prompt box is the input · Ctrl+H types into the terminal".
2. **Full-screen programs** no longer switch to native input by themselves. A floating
   **"Take control (Ctrl+H)"** button appears over the top-right of the terminal while the
   alternate screen is active or an `ssh`/`mosh`/`telnet` session runs
   (`relay::input::offerTakeControl`); the prompt box keeps the focus. The button and Ctrl+H enter
   today's native mode, and leaving the program returns to the prompt box as before. The old
   automatic hand-over is still available per program or globally: Actions › "Control when a
   full-screen program starts" (QSettings `control/default`, whose default flipped from `human` to
   `agent`, and `control/programs`).
3. **Password prompts** (`ICANON` on, `ECHO` off while a foreground program runs) switch the
   prompt box to masked input: a separate `QLineEdit` with `QLineEdit::Password`, a
   `password for <program>` chip, and the route label "PASSWORD · the line goes to the program,
   not to Relay". Enter writes the line plus a newline to the running program's stdin.
   - it never reaches `RichEditor::remember`, the queue, the request ledger, the session file,
     logs, route assist, suggestions or any model prompt: the masked field is not the composer's
     document, nothing typed there is routed, and `relay::input::retainable` is false for it;
   - `relay::input::Secret` keeps a private deep copy, `take()` hands the line over once and wipes
     it, the local copy is wiped after the write, and the field is overwritten and cleared
     (`QLineEdit::setText` also drops its undo history);
   - masked input ends when echo returns for two polls, when the command exits, on Esc, or when the
     user takes control; the chip goes with it;
   - Relay prints nothing about it in the terminal; the status line only names the program.
4. **Ctrl+I at a password prompt** leaves masked input and switches the pane to agent mode
   ("Input: Agent · the password prompt is still waiting"). That path is the normal agent path:
   not masked, and subject to the existing control rules.
5. **Ordinary programs reading stdin.** The rule implemented, applied in `Pane::sendLineToProgram`
   before the router is asked: *an agent submission always goes to the agent; otherwise, while a
   foreground process group other than the shell is running and the terminal is in canonical (line)
   mode, a submitted single-line entry is written to that program's stdin — either because echo is
   off (a password prompt) or because echo is on and a process of the command is blocked in
   `read()` on the tty. Anything else keeps the existing behaviour: run in the shell now, or queue
   until the terminal is free.* A line sent this way shows "Sent to apt" and is never remembered.
   `sudo`, `doas`, `pkexec`, `su` and processes of another user do not expose
   `/proc/<pid>/syscall`, so only their password prompts are detected and everything else queues;
   the composer row says so. "Waiting for input" no longer moves the focus to the terminal.
6. **Hints and wording.** New hints: `terminal.click` (a plain click in the terminal) and
   `control.human.mouse` (pressing the Take control button). The composer's key line now lists
   "Ctrl+H type into the terminal"; the palette entries for Take control, Show the Relay prompt and
   the full-screen control policy were reworded; `terminal.native` (F12) is unchanged and still
   available for the old behaviour.

Docs: `README.md` ("The prompt box is the only keyboard input"), `docs/ARCHITECTURE.md` section 9
(rewritten) and section 5, `docs/VALIDATION.md` (QA lane).

## Implementer check (not a QA verdict)

`docs/qa_evidence/2026-09-17-prompt-box-only-input/drive.sh` under Xvfb, once per engine
(`…/implementer-NN-*.png` for the Relay engine, `…/implementer-NN-*-konsole.png` for KonsolePart). Both runs behaved
identically and `relay-stderr*.log` is empty.

- `implementer-02` clicking the terminal then typing: the text lands in the prompt box, with the hint.
- `implementer-03` a drag in the terminal still selects.
- `implementer-04`/`05` `vim`: the "Take control (Ctrl+H)" button shows and the prompt box still takes typing.
- `implementer-06` Ctrl+H hands the keyboard to vim; `07` Ctrl+Shift+H returns to the prompt box while vim runs.
- `implementer-09`–`11` a program using `getpass`: `password for python3` chip, masked dots, the program
  receives the line ("length 32, starts with 'w'"). `12` Up in the prompt box shows
  `python3 askpass.py`, not the password.
- `implementer-13`/`14` `su root -c true` (an opaque elevator): `password for su`, then
  `su: Authentication failure` for the throwaway password. `sudo -k true; sudo true` did not prompt
  on the test machine (sudo is not configured to ask there), so `13a-sudo` only shows that.
- `implementer-15`/`16` `[Y/n]` from a program reading stdin: the answer typed in the prompt box reaches it
  (`answer='n'`).
- `implementer-17` F12 still gives native input.

Build: `cmake --build build` with no new warnings. `./scripts/test.sh` 382 passed;
`ctest --test-dir build` 10/10 (the new `input` group included).

## QA checklist

1. Click in the middle of the terminal, then type: the characters must appear in the prompt box and
   nothing must reach the shell. Repeat after a command has scrolled the screen.
2. Drag a selection in the terminal, click a link, and scroll with the wheel: all still work, and
   copy-on-select (Actions) still copies.
3. Start `vim`, `htop` and `less`: each shows the "Take control (Ctrl+H)" button, the prompt box
   keeps the focus, and typing goes there. Press the button, then Ctrl+Shift+H, then quit the
   program: the prompt box comes back by itself.
4. `ssh localhost` (or any reachable host): same button, no automatic switch.
5. Actions › "Control when a full-screen program starts" › "Relay takes control for you", then
   `vim`: the old automatic hand-over returns. Set it back.
6. `sudo -k true` then `sudo true` on a machine where sudo asks: the `password for sudo` chip
   appears, the field is masked, a wrong password is rejected by sudo and Relay prints nothing
   about it. Check afterwards that Up in the prompt box, the queue strip, Actions › Tasks, the
   saved conversation under `$XDG_DATA_HOME/relay`, and the pane's agent transcript contain no
   trace of it.
7. At a password prompt, press Ctrl+I: masked input ends, the pane is in agent mode, and the
   program is still waiting. Press Esc at another password prompt: masked input ends without
   answering.
8. At a password prompt, press Ctrl+H: the prompt box hides and the password can be typed in the
   terminal; Ctrl+Shift+H returns.
9. `apt-get --simulate install <pkg>` (or any program asking `[Y/n]` on stdin): type the answer in
   the prompt box, press Enter, and check the status line says "Sent to <program>" and the program
   received it. While a program that is *not* reading runs (`sleep 20`), a submitted command must
   still be queued, not sent.
10. Repeat 1, 3, 6 and 9 in a pane on the other engine (palette › "New pane (Relay engine)", or
    start with `--engine=konsole`): the behaviour must match.
11. F12 still toggles native input, and `terminal.native` can still be rebound.
