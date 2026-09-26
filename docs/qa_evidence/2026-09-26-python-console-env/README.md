# Python console pane: which Python runs the kernel (#83YV)

Owner's decision, 2026-09-26 ("i agree with that order"): the kernel runs in the project's own
`.venv` when it has ipykernel, otherwise in a venv Relay builds with `uv` on the first console
(ipykernel, jupyter-console, numpy, pandas, matplotlib). `relay_core/py_env.py` implements it;
protocol 36.5 describes it.

Why: on the owner's machine the worker's `python3` has no jupyter_client, so the kernel was the
stdlib fallback, the console was a plain IPython with a namespace of its own, and the agent's
`py_run_cell` had no numpy (session 654b1ca4, "No module named 'numpy'"). Earlier evidence
passed only because its driver put a Jupyter venv first on PATH.

This run reproduces the owner's case: `VENV=/nonexistent-no-jupyter`, so the worker runs on
the system `python3` (3.12, no Jupyter), in a fresh isolated profile with no managed venv. The
driver and stub are the same as in `../2026-09-26-python-console-inline/`, and the build is the
verify-slot build of the landed tree.

- The first console built `$XDG_DATA_HOME/relay/python/kernel-py312` (uv's cache was warm, so it
  took seconds) and started `…/kernel-py312/bin/python -m ipykernel_launcher`. The pty runs
  `…/kernel-py312/bin/python -m jupyter_console --existing <file>`.
- `06-agent-42.png`: `x = 41` typed in the console. The agent's `py_run_cell("print(x + 1)")`
  printed `42` there, and `py_variables` listed `x`. One namespace, on a worker without Jupyter.
- `08-interrupt.png`, `09-restart-cleared.png`: Ctrl+C and `py_restart` work on it as before.

Unit coverage: `tests/test_py_env.py` covers the order, the uv and pip builds, a failed build, an
unfinished build and the off switch, with fake commands, so nothing is installed.
`tests/test_python_console.py` never builds: `RELAY_PYTHON_PROVISION=off`.
