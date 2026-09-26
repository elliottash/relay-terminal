# Python console: the same line spacing as a shell pane (#83YV)

Owner: "check that the python console has the same line spacing behavior as the shell console".

`drive-spacing.sh` sends the same agent prompt ("add one to x and print it") in a shell pane
(left) and a Python console (right), under Xvfb with the local stub model.

Before this change the console broke the spacing rule of #5AWD. jupyter console wrote the agent's
own cell (its `py_run_cell` echo) into the middle of the agent's block, ahead of the `▸ stub-1`
header. Its `[agent] In [2]:` label went through prompt_toolkit, so it landed after the code and
beside the next prompt: a row reading `[agent] In [2]: In [3]:`.

Now, from `01-shell-and-console.png`:
- Both panes: the user's line, one blank line, the `▸ stub-1` header, then the tool rows and the
  prose set apart as the gap rule says.
- Console only: after the `✦ 3 tool calls` link, one blank line, then the agent's cell as one line
  (`[agent] In [2]: print(x + 1)`) with its output (`42`), and the `In [3]:` prompt directly
  under it. That is where a shell pane puts its prompt, right under the last line.

How: the pane sends Ctrl+X Ctrl+Y when it opens a block at the console's prompt. The startup file
then leaves other clients' output queued. The Ctrl+X Ctrl+P that closes the block writes it out
through prompt_toolkit's `run_in_terminal`, above the repainted prompt.

Test: `tests/test_python_console.py`
`ConsoleRedrawTest.test_the_agents_cell_waits_for_the_redraw_while_relay_holds_the_screen`, in a
real jupyter console pty.

Not from this change: in this fresh profile the shell pane on the left shows no bash prompt, and
it shows two blank rows before `▸ stub-1` where the console shows one.
