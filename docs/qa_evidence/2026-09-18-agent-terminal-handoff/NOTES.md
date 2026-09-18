# The agent hands a command to the terminal: implementer run, 2026-09-18

Card #D8J3. Implementer evidence, **not a QA verdict**. Relay under Xvfb with isolated
`XDG_*`, `TMPDIR` and `RELAY_KEYRING=off` (`launch.sh`), against `stub-provider.py`, a
loopback endpoint that answers `run: <cmd>` / `fill: <cmd>` with a `run_in_terminal` call and a
"Terminal hand-over result" prompt with the exit status it read. `./ask.sh` refuses to run
without a tty (`test -t 0`), asks "Continue?", and exits 0 only for `y`.

| Shot | What it shows |
|---|---|
| `implementer-01-run-started` | `*run: ./ask.sh`: the pane prints "✦ running in your terminal", the script runs with a tty and asks; the existing banner offers "Let the agent drive"; the rest of the agent's reply waits in the strip |
| `implementer-02-follow-up` | answered `y`: "✦ the command finished · its result went to the agent", and the stub's reply "FOLLOW-UP: exit status 0, saw its output" (the prompt carried `Continue? y` / `got: y`) |
| `implementer-03-prefilled` | `*fill: ./ask.sh`: the command is in the prompt box under the one-shot `! terminal` chip |
| `implementer-04-prefill-follow-up` | Enter, answered `n`: follow-up with exit status 1, and no fix attempt although the pane was in terminal mode for that submission |
| `implementer-05-ctrl-c-no-follow-up` | a run interrupted with Esc (`^C`, exit 130): no follow-up request reached the stub |
| `implementer-06-ceiling-prefill-downgrades-run` | `agent/terminal_handoff=prefill`: a `run` lands in the prompt box; the tool result says `downgraded: true` |
| `implementer-07-draft-kept-handover-refused` | a draft typed while the agent was thinking: refused with `draft`, the draft untouched, and the mode back to auto after the earlier prefill was wiped |

Found and fixed during the run: wiping a prefilled command left the one-shot `! terminal` mode
on, so the next prompt (`*slowfill: …`) went to the shell. The chip now leaves with the command.

## One pass with a real model

GLM-5.3 (`glm-coding`), headless through the real `Agent` loop with `context.terminal_handoff:
"agent"` and a stand-in pane that answers `terminal_command`. Three prompts, one turn each:

| Prompt | What the model did |
|---|---|
| "codex on my server `filly` lost its login. Log it in again for me: it's `~/bin/codex login` over ssh, and it prints an SSO URL I have to open. Then remove …/worker/pause so the worker resumes." (the owner's case) | `run_in_terminal {mode: "run", command: "ssh -t filly '~/bin/codex login'", report_back: true}`, with the unpause kept on its todo list for the follow-up turn |
| "I want to delete every node_modules directory under ~/projects … let me look at it and tweak it before anything runs." | `run_in_terminal {mode: "prefill", command: "find ~/projects -type d -name node_modules -prune -exec rm -rf {} +"}` |
| "How many files are in this directory?" | its ordinary tools; `run_in_terminal` not called |

With `max_tokens` at 2048 the first prompt was cut off mid tool call ("Response was truncated"):
a reasoning model needs the usual output room for this tool as for any other.

Not covered here: a real `ssh -t` (localhost has no accepted host key on this machine, and the
owner's `known_hosts` was left alone), a conversation that already holds a fix request (does the
model still reach for a `relay-run` fence?), and the phone.
