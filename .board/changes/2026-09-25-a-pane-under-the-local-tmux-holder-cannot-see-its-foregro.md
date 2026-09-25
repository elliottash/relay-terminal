---
id: Y99T
type: work
status: inbox
labels: [bug, panes]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzy99t
created: '2026-09-25'
source: 'found while working #S976, measured 2026-09-25'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-program-input-mode/], related: [S976], github: null}
---
# A pane under the local tmux holder cannot see its foreground program, so Take control, waiting detection and PROGRAM mode are all off

## Issue
Found while working #S976 (2026-09-25), measured. With `terminal/persistLocal` on (the default) and memory isolation unavailable (no systemd user bus: a sandboxed profile, a container, a CI runner), `startTerminal` runs the pane's shell inside the #87HB tmux holder. The pane's pty then belongs to the tmux *client*, so `Pane::foregroundPid()` (tcgetpgrp on that pty) is the client, `processBusy()` is false with `python3` at its `>>>` prompt (`relay-drive panes` gives `"busy": false`), and everything keyed off it is inert: `setMode("program")` refuses, "Type into it from here" and Take control are never offered, the waiting poll never runs, and no `foreground_program` goes with a route. `pstree` of that Relay shows only `python3` (the worker) and `tmux: client` under it; the shell and the REPL live under the tmux server, outside Relay's tree. On the owner's desktop isolation is on and available, so no holder runs and none of this shows.

The holder knows the real pane: `tmux display -p -t <session> '#{pane_pid}'` gives the shell inside it, and that shell's `/proc/<pid>/stat` field 8 (tpgid) is the foreground group of the inner pty, which is what every rule here needs.

## Done means
- In a pane running under the local holder, with `python3 -q` at its prompt, `relay-drive panes` reports it busy, the busy row offers "Type into it from here", and `input.modeProgram` switches the chip to `PROGRAM · python3`.
- Take control and the waiting hint behave as in a pane with no holder.
- Fails if `processBusy()` is still false under the holder while a program runs (the #S976 drive, `docs/qa_evidence/2026-09-25-program-input-mode/drive.sh`, without its `persistLocal=false` line).
