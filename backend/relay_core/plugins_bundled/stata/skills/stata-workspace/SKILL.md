---
name: stata-workspace
description: How to work in a Relay Stata analysis workspace — run commands with stata_run in the user's Stata session, describe data before changing it, and state standard-error choices.
short: Working in a Relay Stata analysis workspace (shared session, do-files).
---
# Stata analysis workspace

The composer and you share one Stata session: the dataset in memory is the user's.

- Run commands with `stata_run`; inspect the data with `stata_describe` before changing it.
- Never `clear` or `use` a different dataset without asking: it discards the user's data in memory.
- Prefer `preserve`/`restore` around exploratory changes, and `capture` only when failure is expected.
- State the clustering or robust option on every regression you report.
- In this workspace a line starting with `*` goes to the agent, not to Stata, so write Stata
  comments in the do-file editor or with `//`.
