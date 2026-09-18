---
id: TP3W
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, shell-integration]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (GUI D subagent), 2026-09-17
rank: qp
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
source: '`issues/feature_intake.txt` (2026-09-17): "dont hide the prompt box when any program is running, eg sudo apt upgrade, so i can queue agent / terminal commands. i would say, only hide the prompt box for full screen apps and for password input."; owner decision 10 (supersedes "all programs hide the prompt")'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Prompt box stays visible for ordinary programs; hides for full-screen, password and remote sessions; waiting-for-input focus

## Behavior as implemented

- The prompt box no longer hides when a command starts, and focus stays in it after a composer-submitted command so more items can be queued.
- **Full-screen programs:** Konsole's Session emits `primaryScreenInUse(bool)` when a program enters or leaves the alternate screen. Relay connects to it (a small `ScreenWatcher` QObject; the Session object is found through its D-Bus registration) and hides the prompt box while the alternate screen is active (vim, less, htop, tmux), unless the per-program policy gives the agent control. Verified with a probe: `printf '\e[?1049h'`, `less`, and `vim` toggle it; a Python REPL does not.
- **Password prompts:** echo off with canonical input on (polled every 250 ms while a command runs) hides the prompt box; once echo is back on for two polls the prompt box returns even if the command continues.
- **Remote sessions:** `ssh`, `mosh`, `mosh-client`, `telnet` (foreground program name) hide the prompt box, overridable by the per-program policy. They never switch screens, so they cannot be detected otherwise.
- **Waiting for input:** when a process of the running command is blocked in `read()` on the pane's terminal (`/proc/<pid>/syscall` = SYS_read on an fd pointing at the tty, sustained for two polls), Relay shows "Waiting for input · typing goes to the program" and moves focus to the terminal while the prompt box stays visible; when the read ends, focus returns to the prompt box if Relay had moved it.
- Ctrl+C in the prompt box with nothing selected, while a program runs, sends one interrupt to the program.

## Limitations

- `sudo apt upgrade`: the `[Y/n]` prompt is not detected. The reading process runs as root (its `/proc/<pid>/syscall` is unreadable), and sudo ≥ 1.9.14 uses its own pty. Press Ctrl+H or click the terminal to answer.
- A Python/Node REPL is not full-screen; it gets the waiting-for-input focus instead of hiding the prompt box.
- Programs that wait with `poll()`/`select()` rather than `read()` are not reported as waiting.

## Implementer check (not a QA verdict)

Under Xvfb (`docs/qa_evidence/2026-09-17-prompt-visibility/`): prompt visible during `sleep 7`; hidden in `vim -u NONE`, back after `:q!`; hidden at `read -s -p "Password: "`, back after typing while `sleep 5` continued (`got 7`); `read -p "continue? [Y/n] "` showed the waiting hint, typed `y` reached the program (`answer=y`), focus returned to the prompt box.

## QA checklist

1. `sudo apt update` (password), `make`/`npm install`, `sleep 30`: prompt box visible except while typing the password.
2. vim, less, htop, tmux, `man ls`: hidden while open, back on exit; with the per-program policy set to agent the prompt stays.
3. `ssh localhost`: hidden; `exit`: back.
4. `read -p`, `python3` REPL: waiting hint and focus; typing answers; focus returns.
5. Queue commands while `sleep 20` runs; they run afterwards.

## 2026-09-17 update: sudo and programs Relay cannot inspect (GUI E1, owner decision)

- While the foreground program is `sudo`, `doas`, `pkexec`, `su` or `run0`, runs with another real or effective uid, or has an unreadable `/proc/<pid>/syscall` while the shell's is readable, the prompt box stays visible but focus moves to the terminal. The prompt row shows "sudo is running · typing goes to the program · Ctrl+Shift+H to type a prompt" (shortcut text from the keymap; the program name is the real one).
- Ctrl+Shift+H moves focus to the prompt box so terminal and agent items can be queued; the hint becomes "sudo is running · prompts queue until it exits · Ctrl+H or click the terminal to type into it". Ctrl+H in the prompt box or a click returns focus to the terminal (the prompt box stays).
- Password prompts still hide the prompt box; when echo returns, focus goes back to the terminal (not the prompt box) while sudo runs.
- When the program exits, the hint goes away; focus returns to the prompt box unless you typed into the terminal while it ran, in which case it stays in the terminal.
- The waiting-for-input detection is skipped for these programs (it cannot see them).

Implementer check under Xvfb (`docs/qa_evidence/2026-09-17-sudo-focus/`), with fake cases because sudo cannot run non-interactively here:
- `sleep 4`: no hint, focus stays in the prompt box.
- `bash -c 'exec -a sudo sleep 8'`: hint, terminal focus (`implementer-fake-sudo-hint-terminal-focus.png`); Ctrl+Shift+H → prompt box, queued `echo queued-after-sudo`, Ctrl+H → terminal (`implementer-ctrl-shift-h-composer-focus.png`, `implementer-queue-while-sudo-ctrl-h-back.png`); on exit the queued command ran and the hint cleared.
- Unreadable case: `python3 -c 'import ctypes,time; ctypes.CDLL(None).prctl(4,0); time.sleep(8)'` (PR_SET_DUMPABLE 0 makes `/proc/<pid>/syscall` unreadable): "python3 is running" hint, then focus back to the prompt box.
- `bash -c 'exec -a sudo bash -c "read -p Continue? a; echo got=\$a"'`: typed `y` Enter reached the program (`got=y`) and focus stayed in the terminal afterwards.
- Sequence: `implementer-sequence-plain-sudo-undumpable-typed.png`. Not verified: a real `sudo apt upgrade` and a different-uid process.

QA checklist additions:
6. `sudo apt update`: type the password, answer prompts in the terminal; Ctrl+Shift+H, queue `echo done`, Ctrl+H; on exit `echo done` runs.
7. `sudo -i` (a root shell): hint stays while it runs; `exit` clears it.
