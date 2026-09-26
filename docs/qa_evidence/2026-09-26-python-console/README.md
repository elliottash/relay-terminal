# Python console pane (#83YV)

Run under Xvfb using `drive-python-console.sh`, a test venv with `ipykernel` and
`jupyter-console`, and the local `stub-provider.py` as the model endpoint. The app, worker,
kernel, pty, router, and Python tools were real. The stub requested `load_tools`, `py_run_cell`,
`py_variables`, and `py_restart` through the ordinary agent protocol.

- `03-console-attached.png`: the new pane runs Jupyter console and carries the Python · ipython chip.
- `04-x-is-41.png`, `05-tab-completion.png`: composer code reaches IPython and Tab completes `pri` to `print`.
- `06-agent-42.png`: `x = 41` in the pty, then the agent's `py_run_cell` printed `42` in that same pty; `py_variables` reported `x = 41` in the tool result.
- `08-interrupt.png`: Ctrl+C in native terminal input interrupts `time.sleep(30)` with `KeyboardInterrupt` and returns a prompt.
- `09-restart-cleared.png`: the agent's `py_restart` returns, and evaluating `x` in the still attached console gives `NameError` and a usable prompt.
- `10-menu.png`: both console actions are in the pane menu. Stata is not installed on this host; its activation and unavailable path have mocked tests in `tests/test_python_console.py`.

The screenshot captures are from one final drive. `panes.json` records the two panes in the tab.
The driver uses an isolated temporary profile and workspace, which it removes on exit.
