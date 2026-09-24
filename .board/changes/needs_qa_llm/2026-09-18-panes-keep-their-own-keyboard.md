---
id: J314
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code, milestone review), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: 'With two panes side by side, text typed in one pane stays in that pane when a command finishes, or a program asks for a password, in the other; a password prompt in the pane being typed in still takes the keyboard and gives it back; `ctest` (29) passes'
source: 'milestone review of the code base, 2026-09-18: found by reading Pane''s timer-driven paths, then reproduced under Xvfb'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-panes-keep-their-own-keyboard/'], related: [], github: null}
---
# A pane that is not being typed in never takes the keyboard

## Report

Two panes side by side. Start something slow in the left one, move to the right one and type.
When the left pane's command finishes, the keyboard jumps to the left pane's prompt box and the
rest of the sentence lands there (`implementer-c-…-before.png`: "typed-in-RIGHT-before" on the
right, "+AND+AFTER" on the left).

The same thing with a worse ending: if the left pane's program asks for a password instead
(`sudo`, `ssh`, `git push`), the rest of the sentence lands in the left pane's **masked** field
(`implementer-a-…-before.png`: ten dots), where it cannot be read back and is one Enter from
being handed to the program.

Root cause: three places in `Pane` call `setFocus()` from a timer, with no check that the
keyboard is in this pane at all —

- `refreshShellReady()`: `m_refocus` is set for every command Relay runs (`loaded`) and spent on
  the next ready prompt, up to hours later;
- `enterSecretMode()` / `leaveSecretMode()`, driven by the 1 s / 250 ms password poll.

All three were written for the one-pane case, where "put the keyboard back in the input" is
always right.

## Change

`Pane::holdsFocus()`: the keyboard is in this pane, or will be when its window is active again
(`window()->focusWidget()`, so an inactive window still answers correctly — `ownsKeyboard()`,
which voice uses, asks `QApplication::focusWidget()` and is null then). The three sites take the
keyboard only when it is already theirs; for the masked field the question is asked *before* the
prompt box hides, because hiding the focused widget is itself a focus move.

Nothing else changes. The background pane still switches to its masked field, still posts
"Password prompt" to the bell, and `focusInput()` already sends the keyboard to the masked field
when the user does go there.

Files: `src/main.cpp` (`holdsFocus`, `refreshShellReady`, `enterSecretMode`, `leaveSecretMode`).
No protocol change.

## QA checklist

1. **Command finishes elsewhere.** Two panes. Left: `sleep 6; echo done`. Move right
   (Alt+Right) and type continuously for ten seconds. Every character is in the right pane's
   prompt box; the left pane shows "done" and its toast, and did not take the caret.
2. **Password prompt elsewhere.** Left: `sleep 5; sudo -k true` (or the `getpass` line in
   `drive-password.sh`). Move right and keep typing. Nothing lands in the left pane; the left
   pane shows the orange "password for …" chip and an *empty* masked field; the bell has a dot.
3. **Go and answer it.** Alt+Left: the caret is in the masked field, not the hidden prompt box.
   Type the password, Enter: the program gets it, and the keyboard is back in the left prompt box.
4. **One pane, unchanged.** A password prompt in the pane you are typing in takes the keyboard
   at once and gives it back afterwards (`implementer-e-…png`).
5. **Another tab.** Same as 1 and 2 with the slow command in a *background tab*: the visible
   tab's pane keeps the keyboard.
6. **Inactive window.** Start a password prompt, Alt+Tab to another application before it
   appears, come back: the caret is in the masked field without a click.
7. **Esc declines.** Esc in the masked field still returns to the normal prompt box with the
   keyboard in it.
