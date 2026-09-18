# Implementer evidence — screen-text input detection (#YR21) and the agent driving programs (#C1HH)

2026-09-17, this worktree, Xvfb `:95` (free; `/tmp/.X11-unix` held `X0`, `X91`, `X93`),
`Xvfb :95 -screen 0 1700x1000x24`, isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME` under a scratch
directory, scratch workspace, model **kimi-k3** with the key read by the worker from the desktop
keyring (no key ever reached the driver, the screenshots or this file). Screenshots are captures
of the **window id** (`import -window <id>`): a root capture on Xvfb comes back black.

Reproduce with `run-setup.py` then `run-drive.py all` (and `run-drive.py konsole`), both copied
here as they ran. `run-setup.py` also writes the four scripts the agent is asked to answer; each
is a real foreground program, because bash's own `read` builtin is not a process group Relay can
see.

| Shot | What it shows |
|---|---|
| `01-read-p-detected.png` | `./ask.sh` (`read -p "Continue? "`). Banner: **"bash is asking: Continue?"** with "Let the agent drive"; composer row: "… · Enter sends your answer to it" |
| `02-read-p-answered-by-agent.png` | "answer it with yes" + Ctrl+Shift+G. `✦ bash handed to the agent`, `⚙ type_into_program`, `✦ typed: yes⏎ · Answering the script's "Continue?" prompt…`, terminal shows `Continue? yes` / `got: yes`. The agent's second, redundant attempt is refused: *No program is running in the user's terminal pane* (the script had exited) |
| `03-apt-style-question-detected.png` | `apt-get --simulate upgrade` output word for word, then `Do you want to continue? [Y/n] `. Banner: **"bash is asking: Do you want to continue? [Y/n]"** |
| `04-apt-style-answered-by-agent.png` | The agent typed `y`; the script printed `Inst curl …` / `answer was: y` |
| `05-question-before-the-password.png` | `./ask-then-password.sh` at its `[Y/n]` question, before the password prompt |
| `06-password-prompt-refused.png` | The agent answered the question, the script then ran `read -s -p "[sudo] password for elliott: "`. Relay switched the prompt box to masked input (`password for bash` chip), printed `✦ bash is asking for a password · the agent stopped typing`, and the next write was refused: *That prompt is asking for a password or passphrase. Relay never types into a masked prompt*. The agent's answer: "I never type into password or passphrase prompts, even when the password is given to me in the request" |
| `07-script-asking-repeatedly.png` | `./three-questions.sh`, five `[Y/n]` questions in a row |
| `08-agent-driving-banner.png` | Banner while driving: **"Agent driving bash · 4 keystroke(s) · bash is asking: Question 5: keep going? [Y/n]"** with "Take over (Ctrl+H)"; composer row "The agent is driving bash · Ctrl+H takes it back" |
| `09-user-took-over.png` | Ctrl+H mid-turn |
| `10-write-refused-after-take-over.png` | `✦ you took bash back from the agent`, then the agent's next write: `✦ not typed: The user took control of the terminal, so nothing was typed. Stop typing and let them drive.` The agent stopped and said so |
| `11-konsolepart-no-screen-text.png` | The same `./ask.sh` in a **KonsolePart** pane (`--engine=konsole`). This KF5 KonsolePart has no `getDisplayedText`, so the pane reports no `ScreenText`: the banner degrades to **"bash is asking for input"** (the /proc signals only, exactly the behaviour that shipped before) and "Let the agent drive" is disabled |
| `12-konsolepart-delegation-refused.png` | Ctrl+Shift+G there: *"This pane runs on KonsolePart, which does not let Relay read the screen, so the agent cannot see the program. Open a Relay-engine pane to hand a program over."* |

## What this does not cover

- No live `sudo` run: this box's QA profile cannot answer a real password prompt, so the
  sudo/apt case is covered by the classifier fixture `tests/fixtures/screen/apt-continue.txt`
  plus `tests/inputpolicy_test.cpp::screenTextCanStandInForTheProcProof` (canonical input with
  echo and **no** `/proc` reader, which is exactly what sudo leaves behind).
- No live full-screen editor (vim, `git rebase -i`) driven by the agent. The pieces are there
  (`key` presses, `submit: false`, the alternate screen is an allowed target) but it was not
  run end to end.
- kimi-k3 repeatedly called `type_into_program` again after the program had already exited and
  then narrated the refusal as though its first, successful write had failed. Relay's behaviour
  was right at every step (the answer is in the terminal, the refusal names the real reason).
  The tool result carries `program_exited: true` and "do not call this tool again for it" when
  the pane can already see the program is gone, but the pane answers 400 ms after the write and
  a script that exits a moment later slips past that. Worth re-checking with GLM and DeepSeek
  during QA; it is a narration problem, not a control problem.
