---
id: ZK66
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session, pane UX subagent), 2026-09-17
rank: q4
created: '2026-09-17'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: '`issues/feature_intake.txt` ("have an option for ''agent stays in control with programs'', and also allow that to be set separately by program")'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# "Agent stays in control" option, per program

## Behavior as implemented

- QSettings `control/default` = `human` (default) or `agent`; `control/programs` maps a program basename to `human` or `agent`.
- 150 ms after a command starts, Relay reads the foreground program's basename. If the policy is `agent`, the prompt stays, focus stays in the prompt box, and a toast reads "Agent in control of <name> · Ctrl+H to take control". Otherwise the existing human-control behavior applies.
- Password-prompt detection still forces human control.
- Palette: Terminal › Control when a program starts (You take control / Agent stays in control); while a program runs, "Always give the agent control of <name>" and "Always take control of <name>" (selecting a checked one clears the override).

## Implementer check (not a QA verdict)

Xvfb: `less short.txt` hid the prompt (default); after Actions › Always give the agent control of less, running `less short.txt` again kept the prompt and showed "Agent in control of less · Ctrl+H to take control". Settings stored `control/programs = {less: agent}`.
Evidence: `docs/qa_evidence/2026-09-17-program-control-policy/implementer-less-human-then-agent.png`.

## QA checklist

1. Default "Agent stays in control": `vim` keeps the prompt; Ctrl+H takes control; `sudo true` still hands control to you at the password prompt.
2. Per-program override beats the default in both directions.
3. Programs launched through wrappers (`env`, `sudo`, shell scripts) resolve to a sensible name.
