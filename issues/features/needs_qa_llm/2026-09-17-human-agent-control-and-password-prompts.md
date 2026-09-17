---
id: 9D0B
type: work
status: needs-qa-llm
component: [gui, shell-integration]
milestone: desktop-alpha
workstream: terminal
assignee: implemented by Claude Opus 5 (Claude Code session), 2026-09-17
rank: jq
created: '2026-09-17'
acceptance: a non-Claude model QA session runs the checklist on a real desktop and records it under `docs/qa_evidence/`
source: '`issues/feature_intake.txt` control items; `issues/bug_intake.txt` "when i was in vim, i couldnt get back to the terminal with a keyboard shortcut"; owner decisions of 2026-09-17'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Human and agent control toggle, auto control for running programs, password detection

## Owner decisions (2026-09-17)

- Ctrl+H puts the human in control; Ctrl+Shift+H brings back the prompt and puts the agent in control.
- All programs, not only full-screen ones, hand control to the human (passwords are not full-screen).
- Research on Warp's handling: `docs/CONTROL-AND-FILE-PANES-RESEARCH.md`.

## Behavior as implemented

- A command still running after 150 ms hides the prompt box and focuses the terminal; the prompt returns when it ends.
- Ctrl+Shift+H (acts inside programs) shows the prompt; submissions go to the agent while a program runs.
- Ctrl+H acts only from the prompt box; in the terminal it stays Backspace.
- Password prompt: echo off + canonical input on, polled every second while a command runs; puts the human in control with a notice.
- Background window: taskbar alert and `notify-send` for password prompts and commands longer than 30 s.
- Konsole's terminal size overlay is disabled in Relay's profile (it flashed whenever the prompt hid).
- Resolves the bug report: Ctrl+Shift+H returns from vim to the prompt.

Not yet: the agent typing into running programs (needs screen access; see the delegate issue).

## Implementer check (not a QA verdict)

2026-09-17 under Xvfb: `sleep 3` hid the prompt and restored it; vim hid it; Ctrl+Shift+H showed
the prompt over vim with "Agent in control · Ctrl+H to take control"; Ctrl+H returned to vim;
`:q!` restored the prompt; `read -s -p "Password: "` showed "Password prompt · you're in control"
and the typed password reached the program (`got 7`).
Evidence: `docs/qa_evidence/2026-09-17-control-and-passwords/implementer-password-prompt.png` (the handoff screenshots were not kept).

## QA checklist

1. `sudo -k; sudo true` shows the password notice; the password reaches sudo.
2. `ssh` host key and passphrase prompts; `python3` REPL; `htop`; `less`.
3. Background the window during `sleep 40`; a notification arrives when it ends.
4. Quick commands (`ls`) do not visibly flicker the prompt.
5. With a program running, text submitted from the prompt goes to the agent, never the program.
