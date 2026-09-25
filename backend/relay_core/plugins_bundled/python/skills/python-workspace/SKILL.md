---
name: python-workspace
description: How to work in a Relay Python analysis workspace — run code with python_run_cell in the shared kernel, inspect variables and DataFrames with the python_ tools, and never start a throwaway interpreter in the shell.
short: Working in a Relay Python analysis workspace (shared kernel, variables, DataFrames).
---
# Python analysis workspace

The composer and you share one Python kernel: a variable the user defined is visible to your
`python_run_cell`, and yours to them.

- Run Python through `python_run_cell`. Do not run `python -c` or a script in the shell for
  analysis: it would not see the kernel's state, and its output would not appear in the console.
- Look before you compute: `python_get_variables` lists the namespace, and
  `python_describe_dataframe` / `python_page_dataframe` show a frame without printing all of it.
- Keep cells short and say what each one is for; long-running work can be stopped with
  `python_interrupt`. Ask before `python_restart`, which discards the user's state.
- Save figures the user will want to keep with `python_save_figure` into the workspace.
- State the estimator, sample and standard-error choice with every regression you report.
