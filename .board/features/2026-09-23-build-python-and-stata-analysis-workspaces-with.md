---
id: 33G0
type: work
status: planned
labels: [feature, plugins, python, statistics]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
blocked_by: [MDA7]
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23; implementation slice from reports/Editable workspaces for Relay.md
links: {plans: [], commits: [], evidence: [], related: [MEPR, P2W8, F8R7, E85D, C0Q8], github: null}
---
# Build Python and Stata analysis workspaces with shared execution state

## Issue
then lets go to your reports and write detailed cards for the editable artifacts and plugins features

## Discussion points
**Second task plugin from #MEPR.** Two stages share the same language-router interface. Stage 0 makes the composer understand an already-running Python/IPython or Stata REPL in the visible terminal; typed code reaches that program, natural-language requests reach the agent, and a person can take control back. The full workspace owns a persistent kernel/session so human cells, script cells and agent `run_cell` calls share state and render structured output. Python with ipykernel is the first complete implementation; Stata follows through a validated installed bridge (`stata_kernel`, `pystata` or console), not a hard-coded assumption about license/version. Reserve Relay's forced agent/shell prefixes consistently; explicitly define how Stata `*` comments and IPython `!` escapes are entered. The console retains a Bash terminal for ordinary commands; `!` forces it. Plots and tables should reuse #MDA7's media layer when available.

## Done means
- With a known foreground Python/IPython or Stata REPL, auto mode identifies runnable statements and sends them to that program even when it uses raw tty input; a natural-language request goes to the agent. The UI shows the chosen destination before submit; multi-line paste, interrupt, takeover and forced shell/agent modes work.
- A Python workspace starts one persistent, interruptible kernel for a tab. Composer statements, `# %%` script cells and the agent's `run_cell` tool execute in that same state, with ordered input/output/error records and visible agent intent. Restart makes state loss explicit.
- The workspace shows variables and previews DataFrames/plots through typed output adapters; export contains enough code, outputs and ordering to reproduce the session without exposing secret input.
- Stata activation detects installed bridge/version/license availability and gives a clear supported path or a console fallback. The chosen Stata bridge passes the same routing/state/interrupt tests on a machine where Stata is available; absence of Stata is reported, not treated as a passing integration test.
- Router, foreground-input, kernel state, tool parity, output and restart tests pass; a live Python analysis scenario proves human and agent share one value.

## Plan
**Goal.** The composer, the agent and a person share one interactive analysis state. Python is done except the rich views. Stata follows on a validated bridge.

**State (audit 2026-09-26).** Most of this card was delivered through the slices #P2W8 cut from it. Landed:
- Router and kernel, in `9da682de`. `lang_router.py` classifies Python, IPython and Stata. `py_kernel.py` runs one session per workspace, on Jupyter when available and on a stdlib fallback otherwise.
- The worker, in `13909113` (#C0Q8). It has the `WorkspaceManager`, the Python `KernelRuntime`, and `py_run_cell`/`py_interrupt`/`py_restart`/`py_variables`/`py_history`/`py_export` (a `# %%` script or JSON) with native/guest parity. Routing goes through `lang_router`.
- Stage 0, routing into a REPL already running in a shell pane, was delivered by #S976 in `3d3b9587`, `4cc444e3`, `e613d73c` and `441f5307`. PROGRAM mode, `foreground_program` on `route`, the program/incomplete branches and Python completion are in. The doubled-prefix rule is implemented and documented at `docs/TASK-PLUGINS.md:188` and `docs/AGENT-SESSIONS-PROTOCOL.md:9283`: `!!` hands `!` to the program and `**` hands `*`.
- The Python console, delivered by #83YV in `5b9ce8cd`, `cf38b781`, `3d4233e0`, `f8359d95`, `e405d4d7`, `ba0b8e1f`, `f629ec4c` and `dd484d8a`. It is `jupyter console --existing` on the shared kernel, and the composer's code goes to the pty. Live evidence shows `x = 41` typed and `py_run_cell` printing 42, plus interrupt and restart. The kernel's Python comes from the project venv, then the worker's Jupyter, then a Relay-managed uv venv. The Stata console action falls back to a mocked "unavailable" path. Ctrl+Alt+E opens the new-pane chooser.

Still missing (the "Implement python console kernel environment" work is `f629ec4c` and is on main; the uncommitted `src/` hunks in the tree are #2FQ9's, not Python):
- There is no Variables pane. The role exists (`src/ArtifactWorkspace.h:39`) and the worker emits `kernel_variables`/`kernel_record` (`workspace_plugins.py:314-327`), but `rg kernel_variables src/` is empty.
- Plots and tables never render in Relay. The kernel records `display_data` (`py_kernel.py:814`), but the console is jupyter console in a pty, which prints `<Figure …>`.
- A `.py` editor has no way to run a `# %%` cell in the console's kernel. The manifest's commands are only `/restart`, `/vars` and `/export`.
- There is no Stata bridge. `relay.stata` runs `stata -q` as a console, and `stata_kernel`/`pystata` are neither chosen nor validated (#MEPR decision 2 is open). No Stata is installed here.
- A test bug from `f629ec4c`: with `~/.local/share/relay/python/kernel-py312` present, three tests in `tests/test_workspace_plugins.py` fail (`test_python_workspace_routes_code_questions_and_forced_prefixes`, `test_worker_activates_routes_and_runs_a_kernel_line`, `test_deactivate_and_shutdown_close_the_kernel`). With an empty `XDG_DATA_HOME` all 25 pass. This belongs to #83YV's area and is reported separately; it is listed here because this card's verification runs those tests.

**Split (non-overlapping).**
- #83YV owns the Python and Stata console panes (pty, chip, handshake). Its deferred t:ja is the live check of the Stata console on a Stata host.
- #S976 owns the PROGRAM destination.
- #E85D owns group roles and layouts; #MDA7 owns inline media.
- This card owns the rich analysis views (variables, plots/tables), editor cells into the kernel, and the Stata bridge behind `stata_run`/`stata_describe`.

**Steps.**
1. Variables pane. It becomes a `Role::Variables` member of the console's group. It is fed by `kernel_variables` and refreshed after each cell. A DataFrame row opens a table preview of the first N rows, with shape and dtypes.
2. Rich output. `display_data` and `execute_result` images and HTML tables from any cell, human or agent, render through #MDA7's inline layer, in the console pane's transcript or in a plot preview member (Q1). Matplotlib uses the inline backend in the kernel.
3. Editor cells. A `.py` editor linked to a Python console gets Ctrl+Enter / `/run-cell`, which sends the `# %%` cell under the cursor to `kernel_run` with the editor as origin. `py_history` and `py_export` record that origin.
4. Stata bridge. Probe the Stata version and edition, then pick a bridge by the answer to Q2. `stata_run` and `stata_describe` are implemented on that bridge, with the console as the fallback. It needs the same routing, state and interrupt tests on a Stata host, and "Stata absent" is reported, never passed.
5. Live scenario. Load a CSV with pandas in the console, have the agent plot a column, see the plot and the DataFrame in the Variables pane, run an editor cell, then export and re-run the script.

**Risks.**
- Rich output must not be printed twice, once in the pty and once in Relay.
- Kernel state and variables stay scoped to one workspace.
- Stata licensing and version differ between machines.
- A plot preview member interacts with #R660's chain order.

**Verify.**
- Protocol and runtime tests for variables, display_data and editor-origin cells.
- `tests/test_workspace_plugins.py`, hermetic, all passing.
- The live Xvfb scenario of step 5, with evidence in `docs/qa_evidence/<date>-analysis-workspace/`.
- Stata only on a host where it is installed.
**2026-09-26 scope update.** Deliver Python plots and tables inline with open-in-preview, then the Variables pane and linked `# %%` cells. Live Stata bridging is #R8Z5 and awaits the owner's Stata version, edition and host. Preserve the doubled-prefix input rule.

## Tasks

- [x] REPL foreground detection and Python/Stata routing (9da682de lang_router; GUI half #S976 3d3b9587, 4cc444e3) <!-- t:v0 -->
- [x] Send code blocks to visible REPLs with safe takeover/interrupt (#S976 3d3b9587, 473a4cfd; console panes #83YV cf38b781) <!-- t:xj -->
- [x] Persistent Python kernel and shared human/agent run_cell (9da682de, 13909113; console on the same kernel #83YV 5b9ce8cd, cf38b781, f629ec4c) <!-- t:zb -->
- [ ] Variables pane with DataFrame table preview <!-- t:9h -->
- [ ] Plots and tables from any cell through #MDA7's inline layer <!-- t:p3 -->
- [ ] Run `# %%` editor cells in the linked console's kernel <!-- t:c5 -->
- [x] Live Stata bridge and console validation transferred to #R8Z5 pending a Stata host <!-- t:gm card=R8Z5 -->
- [ ] Verify Python analysis scenario, inline plots/tables, Variables, editor cells, export and restart <!-- t:wj blocked_by=9h,p3,c5 -->

## Decisions
- 2026-09-26 — Owner: “yes to all.” Build inline plots and tables first, then Variables with a DataFrame preview; include “open in preview” for larger plots. Preserve doubled-prefix input. Use the proposed `pystata`/`stata_kernel`/`stata -q` bridge order when a Stata environment is available. The Stata version, edition and host remain unknown; live Stata validation must be separately tracked.
