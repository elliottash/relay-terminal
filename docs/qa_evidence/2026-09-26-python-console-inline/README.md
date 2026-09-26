# Python console pane: the agent's lines print in the console (#83YV)

The owner, trying the console: "it should be like the shell commands, where the agent messages
print in the console". Before this change they did not: a pane's inline output waits for Bash's
event hook to report the prompt (`m_promptReported`), a console runs no Bash, so every agent line
queued in `m_inlinePending` and showed only in the fallback transcript panel
(`../2026-09-26-python-console/06-agent-42.png`, the "Agent - stub-1" box).

Now `Pane::consoleAtPrompt()` reads the console's own OSC 133 marks (A/B from the bundled
`ipython_startup.py`), inline output prints there, a `B` mark flushes what queued while a cell
ran, and `closeInline()` sends Ctrl+X Ctrl+P, which the startup file binds to a prompt_toolkit
repaint, so IPython draws its prompt again below the agent's lines.

Same driver as `../2026-09-26-python-console/` (`drive-python-console.sh`, `stub-provider.py`),
run under Xvfb against the verify-slot build of the landed tree, with a venv holding ipykernel
and jupyter-console.

- `06-agent-42.png`: `x = 41` in the pty; the prompt "add one to x and print it", the model row,
  the tool rows and the reply are printed in the console itself, IPython's prompt comes back under
  them, and the agent's cell shows as `In [2]: print(x + 1)` / `42`.
- `09-restart-cleared.png`: after `py_restart`, `x` is a `NameError`. Seen here: jupyter console
  repainted its prompt once in the middle of the agent's block (`In [6]: ▸ py restart`) — the
  kernel restart makes prompt_toolkit redraw at a cursor Relay had moved.
- `08-interrupt.png`, `03`–`05`, `10`: unchanged behaviour (interrupt, chip, completion, menu).

## No shell handoff from a console pane

The owner's next report: the console agent "suggested a shell command rather than python" — its
turn ended with `run_in_terminal("xdg-open ~/Pictures/burning_ship.png")`, prefilled into the
Python console's prompt box. A console pane now sends no `context.terminal_handoff`, so the tool is
not offered there. `stub-provider.py` logs `run_in_terminal` per request: a rerun of this driver
had it `False` on every console turn; the same stub on a plain shell pane had it `True`.
