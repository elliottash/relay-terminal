# The prompt box is the only input; clicking the terminal does not type into it

- **Status**: open
- **Component**: gui
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: clicking in the terminal area selects text but does not give it the keyboard; typed keys always reach the prompt box, except in the explicit take-control and full-screen-program cases
- **Assignee**: unassigned
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

## Open question
- While an ordinary (non-full-screen) program is running and it is reading stdin (e.g. a `[Y/n]` prompt),
  should a submitted line go to that program automatically (recommended, consistent with the password case)
  or still be queued as the next shell command?
