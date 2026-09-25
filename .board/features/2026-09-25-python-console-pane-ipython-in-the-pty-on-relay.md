---
id: 83YV
type: work
status: executing
assignee: claude-code
labels: [feature, python, plugins, panes]
rank: zzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (U2)'
links: {plans: [], commits: [], evidence: [], related: [P2W8, 33G0, S976, C0Q8], github: null}
---
# Python console pane: IPython in the pty on Relay's kernel, shared with the agent's py_* tools; Stata after it

## Issue
i would want to prioritize building a python console and a stata console. we can build python first.

## Plan
Slice 6 of #P2W8 (decision U2). **Wave 2: waits for the hold on #C0Q8/#33G0's `workspace_plugins.py` to lift** (the `KernelRuntime` that owns the kernel lives there, untracked in the tree), and for #S976 (the Program destination) and #6FDD (manifest v2 `console.program`).

**Goal.** "New Python console" opens a pane that looks like a shell pane but runs IPython in the pty, attached to the workspace's kernel, so what the user types and what the agent runs through `py_run_cell` share one namespace; Python statements route to the program, prose to the agent; `py_*` tools and the Python skill are active; then Stata the same way.

**Findings.** `Pane::startTerminal` always builds Bash (`src/PaneRuntime.cpp:880-956`); the engine can start any argv (`engine/backend/VTermBackend.cpp:106`). `KernelRuntime`/`KernelSession` start a kernel through `jupyter_client` when installed (`workspace_plugins.py:136`, `py_kernel.py:835`) and expose `py_run_cell`, `py_interrupt`, `py_restart`, `py_variables`, `py_history`, `py_export` lazily when a workspace is active (`agent.py:1531`). `lang_router.detect_repl`/`classify_line` already classify Python (`lang_router.py:604`, `:644`). No caller in C++ sends workspace activation.

**Steps.**
1. A pane program spec: `Pane::startTerminal(ProgramSpec)` where the spec comes from the plugin's `console.program` (with `{connection_file}` filled from the kernel) or defaults to the shell; header chip "Python · ipython".
2. Activation from the GUI: "New Python console" (palette, and the `New shell` menu) sends `workspace_activate {plugin: relay.python, tab}` (protocol 36) to the tab's worker, waits for the kernel's connection file, then starts `jupyter console --existing <file>` in the pty; without `jupyter_console`, plain `ipython` and the tools type into the pty through the Program path.
3. Prompt marks: the manifest's `console.startup` IPython file installs a `Prompts` subclass emitting OSC 133 A/B/C/D, so blocks, the busy lamp and "waiting for input" work as in Bash.
4. Routing: with the Python console active, `route` carries `foreground_program` and the worker's `program`/`incomplete`/`agent` verdict drives `dispatch` (#S976); the composer's Tab uses the manifest's completion table (#6FDD).
5. Variables: the `variables` role of the manifest's pane roles becomes a small tool pane fed by `py_variables` after each block (optional in this slice; #33G0 t:9h).
6. Stata: `stata -q` in the pty, `stata_run` through the validated bridge or typing into the pty; only where Stata is installed (#33G0 t:gm).

**Files.** `src/PaneRuntime.cpp` (`startTerminal`), `src/Pane.h` (program spec, header chip), `src/AppCommands.cpp` (the action), `backend/relay_core/workspace_plugins.py` (after the hold: connection file in the activation answer), `backend/relay_core/plugins_bundled/python/*`, tests `tests/test_workspace_plugins.py`, `tests/consolemode_test.cpp`.

**Verify.** Live under Xvfb with ipykernel installed: type `x = 41` in the console, ask the agent "add one to x and print it", the pty shows `42`; `py_variables` lists `x`; Ctrl+C interrupts a `sleep`; restart clears `x`. Evidence under `docs/qa_evidence/<date>-python-console/`.

## Tasks

- [ ] Pane program spec and header chip <!-- t:4t blocked_by=#6FDD -->
- [ ] GUI activation and jupyter console --existing on the kernel (after the hold lifts) <!-- t:8m blocked_by=4t -->
- [x] IPython startup with OSC 133 prompt marks <!-- t:wz blocked_by=4t -->
- [ ] Routing and completion through #S976 and #6FDD <!-- t:dq blocked_by=#S976,8m -->
- [ ] Live evidence: shared namespace, interrupt, restart <!-- t:7c blocked_by=8m,wz,dq -->
- [ ] Stata console where Stata is installed <!-- t:ja blocked_by=7c -->
