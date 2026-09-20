---
id: FW77
type: work
status: deferred
labels: [feature]
component: [agent, router]
milestone: cross-platform
workstream: agent
rank: '84'
created: '2026-09-17'
acceptance: a `relay-tui` command runs inside any terminal emulator (including over SSH), gives a shell pane plus the Relay prompt with command/agent routing and inline agent output, using the same backend worker as the Qt app
source: '`issues/feature_intake.txt`, "would be interesting to have a version that works purely in the terminal like croft"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Relay as a pure terminal (TUI) app

## Context

"croft" is most likely croft (https://docs.croft.software/, github.com/vitali87/croft): a VS Code-style
three-pane IDE (explorer, editor, panel with a real terminal) that runs entirely inside a terminal,
Rust, MIT, with MCP/AI pair mode. A TUI Relay would reuse `backend/worker.py` and `relay_core`
(agent, router, providers) and replace the Qt shell. Options and trade-offs in
`docs/NEXT-STEPS-RESEARCH.md` section C. Needs an owner decision on scope before work starts
(exploratory; "would be interesting").
