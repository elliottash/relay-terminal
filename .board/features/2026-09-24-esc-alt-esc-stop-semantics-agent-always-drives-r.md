---
id: H2KQ
type: work
status: needs-verification
labels: [feature, program-control, keyboard]
implemented_by: kimi/kimi-k3
rank: zzzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Esc/Alt+Esc stop semantics, agent always drives, retire the program bubble

## Issue
to check -- alt esc shoudl stop running programs, not just cancel shell commands, make sense? 

if a program is running, can we put that next to the relaying - vim... (alt esc to close)

so esc stops shell when no agent is running, but alt esc needed to close a running pgoram, eg vim. 

i also saw, i had to click "let agent drive", but i had decided previously that agent always drives, remove that button.

## Execution Summary
Landed on main as d3cc0d23 (land.py verify build of the exact tree passed).

- **Agent always drives** (`maybeAutoDelegate`, Pane.h; called at the program-start stage in PaneRuntime.cpp): a program starting while an agent is configured is delegated to it automatically and quietly — no toast, no transcript note, no note when the program exits. Explicit hand-offs (Ctrl+Shift+J, palette) still print theirs. A password prompt still ends delegation with its note. This removes the "Let the agent drive" step and overrides #C1HH's per-turn consent by the owner's standing decision.
- **Program bubble retired**: updateTakeControl never shows the top-right bar; its one action moved beside the Relaying line as `busyAction` (PaneUi.cpp) — "Take over (Ctrl+H)" while the agent drives, "Take control (Ctrl+H)" for a full-screen program with no agent driving.
- **Esc/Alt+Esc**: Esc stops a shell command when no agent runs, but a full-screen program (alternate screen) keeps Esc; Alt+Esc closes it. Alt+Esc sends Ctrl+C, and a program still alive on the next press a beat later gets SIGTERM to its foreground process group (`forceInterruptShell`). The queue strip's stop button uses forceInterruptShell and labels the key Alt+Esc when the alternate screen is active.
- **Relaying line**: a running program shows "Relaying · vim… · Alt+Esc stops" (Esc for a shell command), refreshed on alternate-screen transitions.
- docs/ARCHITECTURE.md section 9.2 rewritten for the new model.
- Left uncommitted, deliberately: other sessions' in-flight hunks in the same files (#R5TC pane directory, #MTCS prose trailer) were excluded hunk-by-hunk from the commit.

Foreign work encountered, not touched: tests/boardexecute_test.cpp (session e728) and the #MTCS WindowState.h signature change were half-applied in the shared tree during the work and broke `relay` target builds twice; both were theirs to finish.

## Tasks

- [x] Alt+Esc closes running programs (Ctrl+C, then SIGTERM if it was caught) <!-- t:k7 -->
- [x] Relaying line shows the program and its stop key ("vim… · Alt+Esc stops") <!-- t:g5 -->
- [x] Esc stops a shell command with no agent; full-screen programs keep Esc (Alt+Esc closes) <!-- t:6v -->
- [x] Agent always drives: auto-delegate on program start, "Let the agent drive" gone <!-- t:nb -->
- [x] Top-right program bubble retired; Take over / Take control beside the Relaying line <!-- t:a5 -->
- [x] Tests, docs, land <!-- t:hf -->

## Tests
- `relay-consolemode-tests --h2kq-only` — five new cases in `tests/h2kq_cases.h`: shell command shows "Relaying · sleep… · Esc stops" and "Stop shell (Esc)" and Esc interrupts it; a full-screen program (real alt screen via `printf '\033[?1049h'`) shows "Alt+Esc stops" / "Stop shell (Alt+Esc)", plain Esc does NOT interrupt, Alt+Esc closes it; the top-right bubble (`takeControl`) is always hidden; with an agent configured a starting program shows "Take over" without any click and clicking it hands the keyboard back; with no agent a full-screen program shows "Take control" instead.
- Full `consolemode`, `queuecontract`, `queuenav`, `queuesubmit`, `input`, `backends` ctest suites pass after the change (consolemode run 3x for flake check).
- land.py's verify build compiled the exact committed tree (`--target relay`).

verify: AI may gate this card (per Options: AI gate never) — it waits in needs-verification for the owner. Suggested check: run vim in a pane, see "Relaying · vim… · Alt+Esc stops" with a Take over button beside it, confirm Esc inside vim still edits, Alt+Esc twice closes vim, and no top-right bubble appears.
