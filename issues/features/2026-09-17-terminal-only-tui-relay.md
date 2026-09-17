# Relay as a pure terminal (TUI) app

- **Status**: open
- **Component**: agent, router
- **Milestone**: cross-platform
- **Workstream**: agent
- **Acceptance evidence**: a `relay-tui` command runs inside any terminal emulator (including over SSH), gives a shell pane plus the Relay prompt with command/agent routing and inline agent output, using the same backend worker as the Qt app
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, "would be interesting to have a version that works purely in the terminal like croft"

## Context

"croft" is most likely croft (https://docs.croft.software/, github.com/vitali87/croft): a VS Code-style
three-pane IDE (explorer, editor, panel with a real terminal) that runs entirely inside a terminal,
Rust, MIT, with MCP/AI pair mode. A TUI Relay would reuse `backend/worker.py` and `relay_core`
(agent, router, providers) and replace the Qt shell. Options and trade-offs in
`docs/NEXT-STEPS-RESEARCH.md` section C. Needs an owner decision on scope before work starts
(exploratory; "would be interesting").
