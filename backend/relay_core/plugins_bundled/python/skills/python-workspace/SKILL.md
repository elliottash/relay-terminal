---
name: python-workspace
description: How to work in a Relay Python analysis workspace — run code with py_run_cell in the shared kernel, inspect variables with the py_ tools, and never start a throwaway interpreter in the shell.
short: Working in a Relay Python analysis workspace (shared kernel, variables, DataFrames).
---
# Python analysis workspace

The composer and you share one Python kernel: a variable the user defined is visible to your
`py_run_cell`, and yours to them. The `py` tools arrive with `load_tools` (group `py`).

- Run Python through `py_run_cell`, with an `intent` sentence the user sees beside the cell. Do
  not run `python -c` or a script in the shell for analysis: it would not see the kernel's state,
  and its output would not appear in the console.
- In a Python console pane (header chip "Python · ipython") the person is watching IPython: every
  `py_run_cell` is shown there as an `[agent]` cell with its output, so do the work there too —
  a figure is `plt.savefig(...)` in a cell, not a script written with `write_file` and run with
  `run_command`, and never `run_in_terminal`, which would type a shell command into IPython.
- Look before you compute: `py_variables` lists the namespace with types, shapes and previews,
  and `py_history` shows the cells already run — the user's and yours — with their output.
- Keep cells short; print `df.head()` or `df.describe()` rather than a whole frame. Long-running
  work can be stopped with `py_interrupt`. Ask before `py_restart`, which discards the user's state.
- `py_export` returns the session as a runnable `# %%` script when the user wants to keep it.
- Save figures the user will want to keep with `plt.savefig("figure.png")` into the workspace.
- State the estimator, sample and standard-error choice with every regression you report.
