---
id: 83YV
type: work
status: needs-verification
labels: [feature, python, plugins, panes]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:ashe-ethz-ch
session: d01e396a-cbc2-4f35-9846-b513e016beb6
rank: zzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
verify: {artifact: system, primary: probe, also: [script, ai-visual], human: optional, criteria: 'Open both available console types and confirm shared Python state, routing, interrupt, restart, and visible pane labeling.', sign_off: none, effort: high, stakes: rework, blast: capability}
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (U2)'
links: {plans: [], commits: [104e49df7e01, 67abbcb49095, 488ff0c853f7, cf38b781604d, 3d4233e07d88, f8359d9557be, 7f469118498e, e405d4d71250, ba0b8e1fc781, f629ec4c3cf7, dd484d8a6521, 79d1eedcc0ac], evidence: [docs/qa_evidence/2026-09-26-python-console/], related: [P2W8, 33G0, S976, C0Q8], github: null}
---
# Python console pane: IPython in the pty on Relay's kernel, shared with the agent's py_* tools; Stata after it

## Issue
i would want to prioritize building a python console and a stata console. we can build python first.

## Plan
Slice 6 of #P2W8 (decision U2). The wave-2 holds have lifted: #S976 (Program destination) and #6FDD (manifest v2 `console.program`) are in needs-verification, and the worker half of this card is on main.

**Goal.** "New Python console" opens a pane that looks like a shell pane but runs IPython in the pty, attached to the workspace's kernel, so what the user types and what the agent runs through `py_run_cell` share one namespace; Python statements route to the program, prose to the agent; `py_*` tools and the Python skill are active; then Stata the same way.

**Findings.** Landed on main in 5b9ce8cd (worker half): `workspace_activate {console: true}` starts relay.python's kernel and answers `workspace_console` with the pty argv (`jupyter console --existing <file> --config=ipython_startup.py`, else `ipython`, else `python3`); `JupyterBackend.restart()` keeps the connection file so an attached console follows a restart; the bundled startup emits OSC 133 A/B/C/D (t:wz ✓); ipykernel `ask_exit` no longer kills the shared kernel; `tests/test_python_console.py` (14) covers it under a venv. The **GUI half is written but uncommitted** in the working tree, held by idle land sessions `83yv-gui`/`83yv-gui2` (6h/4h idle): the `ProgramSpec` branch in `Pane::startTerminal` (`src/PaneRuntime.cpp:963`), `askForConsoleProgram()` on `ready` (`:1054`), `onWorkspaceConsole()` (`src/PaneEvents.cpp:69`), action `pane.newPythonConsole` (`src/Keymap.h:270`), the panes-menu item (`src/RelayWindow.cpp:423`, `src/RelayWindowCore.cpp:1005`), the backend entry (`src/TerminalBackends.cpp:124`) — `git log -S workspace_console -- src/` is empty, so none of it is on main. #S976's PROGRAM mode and completion tables are landed, so routing needs wiring checks, not new machinery. No C++ test covers the action or the handshake. Neither `jupyter`/`ipython` nor `stata` is on this machine's PATH.

**Steps.**
1. **Land the GUI half (t:4t, t:8m).** `python3 scripts/land.py who` first, then `begin` a fresh session name on exactly the six GUI paths (Keymap.h, Pane.h, PaneEvents.cpp, PaneRuntime.cpp, RelayWindow.h/.cpp, RelayWindowCore.cpp, TerminalBackends.cpp, AppCommands.cpp as touched) so the stale claims are adopted; review `commit --dry-run` (Pane.h, Keymap.h and RelayWindowCore.cpp carry other live sessions' claims), build with `scripts/relay-build`, run `ctest --test-dir build -R python_console`, then `commit`. A FOREIGN hunk that is this card's GUI work lands with `--take-foreign`; anything that is not, stays out.
2. **Routing and completion (t:dq).** With the console pane live: `route {foreground_program}` puts code typed in the composer into the pty through `handleProgramEvent` and prose to the agent; Tab completes Python through #S976's table; OSC 133 marks drive blocks and the busy lamp. Add a `tests/consolemode_test.cpp` case for the handshake: `ready` → `workspace_activate {console:true}` → `workspace_console` → `startProgram(argv)` → chip "Python · ipython"; the 30s shell fallback arms and rearms. Do not redo #S976's own open verify item.
3. **Live evidence (t:7c).** Under Xvfb, with the worker able to import `jupyter_client` (the test venv, or ipykernel installed for the worker's python): `x = 41` typed in the console, agent asked "add one to x and print it" → `42` in the pty; `py_variables` lists `x`; Ctrl+C interrupts a `sleep`; `Restart console` clears `x` and reattaches. Screens/log under `docs/qa_evidence/2026-09-26-python-console/`.
4. **Stata (t:ja).** Same `ProgramSpec` path with a `stata -q` console kind from a relay.stata manifest (`stata_run` through the validated bridge, #33G0). This host has no Stata, so here it is code + mocked tests only; live checks need a Stata host.

**Risks.** The uncommitted GUI work shares `Pane.h`, `Keymap.h`, `RelayWindow.h`, `RelayWindowCore.cpp` with live sessions (25390d50, 8531c4b7): land only through `land.py commit`, expect the confirm path, and on conflict pull `git show main:<path>` by hand — never a plain `git commit`. The worker resolves `jupyter_client` from its own python: if the live run cannot find it, point the pane's ProgramSpec env at the venv the tests build. Owner decision (question below): whether step 4 lands now as code + mocked tests or waits for a Stata host. The optional variables tool pane (#33G0 t:9h) stays out of this slice.

**Verify.** `tests/test_python_console.py` (14), the new consolemode case, `tests/test_lang_router.py`; then the live Xvfb run of step 3 with evidence under `docs/qa_evidence/2026-09-26-python-console/`.

## Tasks

- [x] Pane program spec and header chip <!-- t:4t blocked_by=#6FDD -->
- [x] GUI activation and jupyter console --existing on the kernel (after the hold lifts) <!-- t:8m blocked_by=4t -->
- [x] IPython startup with OSC 133 prompt marks <!-- t:wz blocked_by=4t -->
- [x] Routing and completion through #S976 and #6FDD <!-- t:dq blocked_by=#S976,8m -->
- [x] Live evidence: shared namespace, interrupt, restart <!-- t:7c blocked_by=8m,wz,dq -->
- [ ] Stata console where Stata is installed <!-- t:ja s=deferred blocked_by=7c -->

## Done means
"New Python console" opens a pane running IPython attached to the workspace kernel, user typing and the agent's `py_*` tools sharing one namespace, checkable live: type `x = 41`, ask the agent to add one and print it, the pty shows `42`; `py_variables` lists `x`; Ctrl+C interrupts a `sleep`; a restart clears `x` and leaves the console usable.
Python statements typed in the composer reach the pty and prose reaches the agent (PROGRAM mode), with OSC 133 blocks, busy lamp and Tab completion working as in a shell pane.
Failure looks like: the console falls back to a shell, the agent's `py_run_cell` comes back `died` after `exit()` is typed, or code typed in the composer lands in chat instead of the pty.
Where Stata is installed, "New Stata console" opens a working Stata pty the same way; where it is not, the action explains why it cannot open.

## Tests
- tests/test_python_console.py::ConsoleCommandTest::test_jupyter_on_path_runs_the_manifest_program_with_the_startup_as_config
- tests/test_python_console.py::ActivationTest::test_stata_console_answers_with_a_program_or_an_explanation
- tests/test_lang_router.py::TableTest::test_python

The Board test runner passed these three at revision 7f469118498e. Separately, `/tmp/83yv-venv/bin/python -m unittest tests.test_python_console` passed all 16 Jupyter console cases, and `python3 -m unittest tests.test_python_console tests.test_workspace_plugins tests.test_lang_router` passed 62 with 6 Jupyter-only skips. The final Xvfb drive in `docs/qa_evidence/2026-09-26-python-console/` shows the shared `42`, `py_variables`, Ctrl+C interrupt, restart clearing `x`, Tab completion, and both menu actions. Isolated exact-tree Relay builds passed in `land.py`.

## Execution Summary
Landed the Python console GUI handshake, ProgramSpec and Python · ipython chip, routing/completion, and Stata console action in cf38b781604d; fixed the Python worker venv import path in 3d4233e07d88 and the console composer watchdog in f8359d9557be. The reproducible Xvfb drive and screenshots are in 7f469118498e. Live evidence shows a Jupyter console attached to the workspace kernel: `x = 41` typed through the composer; the agent's `py_run_cell` prints `42` in the same pty and `py_variables` reports `x = 41`; Ctrl+C interrupts `time.sleep(30)`; `py_restart` clears `x` while the console remains usable. Stata is absent on this host, so its action and unavailable path have mocked tests; its live check is deferred to a Stata host.
