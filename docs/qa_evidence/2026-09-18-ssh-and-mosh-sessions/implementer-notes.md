# #S5SH implementer evidence (not a QA verdict)

Relay under Xvfb with an isolated HOME, XDG dirs, runtime dir and TMPDIR (`driver.sh`), a scripted
model (`fake-provider.py`), and a real `ssh localhost` (key login). Built from HEAD plus this card's
pane and engine edits, 2026-09-18, by Claude Opus 5 (session relay-terminal-be).

| Shot | What it shows |
|---|---|
| `implementer-01` | `ssh localhost` typed in the prompt box, then `echo …; hostname` typed there too: it ran on the host, not queued for the local shell. The remote integration had been typed and erased itself (no bootstrap line visible). Chrome shows the `localhost` chip. |
| `implementer-02` | An agent turn at the remote prompt: the reply is in the terminal under the prompt, and the prompt comes back after it. No side panel. |
| `implementer-03` | "check this on the host": the model called `run_command` with `host: "localhost"`; it ran over the shared connection (`spark-dcc9`, `/home/elliott`, `REMOTE-OK`), no login. |
| `implementer-04` | `exit`: "Shared connection to localhost closed.", the local prompt, the chip gone. |
| `implementer-05` | `ssh/enhance=ask` (`EXTRA_CONF=$'\n[ssh]\nenhance=ask\n'`): the Enhance banner; not taken, so the prompt is found from the screen, and the reply still lands in the terminal with the prompt printed back. |
| `implementer-06` | `read -rsp "Password: "` on the host: the prompt box turned into the masked "password for ssh" field. |
| `implementer-07` | The 7 characters reached the host (`got-7-chars`). `hunter2` appears in neither the model requests nor the logs (`grep -c hunter2 requests.jsonl` → 0). |
| `implementer-08` | `mosh localhost` (the driver with `mosh` for `ssh`): the chip names the host, and the prompt box typed `echo …; hostname` into the session. Before the follow-up the alternate screen hid the login entirely and the line was queued locally, and the chip read "ControlMaster=auto". |
| `implementer-09` | Under mosh the agent's `run_command` ran on the host over the connection mosh's ssh left behind (`ran … on localhost · exit 0`); the reply stays in the side panel by design. |
| `implementer-10` | The same "ask" run after the colour follow-up: the prompt under the reply is bold green and blue again, as the host drew it (in `implementer-05` it came back plain). |
| `implementer-11` | A zsh login (`ssh -t localhost zsh -f -i`): the integration was typed and erased, the prompt box typed on the host, and the reply prints under zsh's `%` prompt. A first run of zsh with no `~/.zshrc` shows its setup menu instead of a prompt; Relay typed into that menu, which is what the `m_screenPrompt.actionable()` guard in `maybeEnhanceLogin` now prevents. |
| `implementer-12` | A tmux on the host (`tmux -f /dev/null new-session`, so its pass-through is off): the prompt box types into the shell inside tmux. Before this, tmux's alternate screen made Relay queue the line for the *local* shell; in between, it was refused with "a full-screen program on localhost has the terminal". The agent's reply stays in the panel here, as under mosh. |

The `implementer-connect-*`, `-options-ssh` and `-split-same-host` shots and `drive-connect.sh` are the
window half (Connect to host, Split on the same host, Options).

Logs of the first run: `login_begin program=ssh resolvable=1`, `login_resolved shared=1`,
`login_enhance bytes=1473`, `login_end`.
