---
id: W5N2
type: work
status: ready
component: [gui, worker, terminal engine]
milestone: desktop-alpha
workstream: terminal
rank: 6d
created: '2026-09-17'
acceptance: from a phone, the owner follows and drives a desktop Relay pane's agent and terminal with notifications; two people share a pane with clear control handoff; all traffic end-to-end encrypted
source: '`issues/feature_intake.txt`, 2026-09-17: "another important feature i need: remote access on phone. multiplayer shared terminals. i like warp remote control and blink but they kind of suck. lets make a good version of that."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Remote access from a phone and multiplayer shared terminals

## Status

Research and design in progress: `docs/REMOTE-AND-MULTIPLAYER-DESIGN.md` (pending).

## Known constraints

- Depends on Relay's own engine (`docs/ENGINE.md`): KonsolePart cannot serialize screen state.
- Owner (2026-09-17): "the no account / cloud / etc are not strict constraints. ideally it could be done in a browser on relay-terminal.ai as a first version, later on we make android / phone apps." A Relay-operated service and accounts are acceptable; v1 is a browser client on relay-terminal.ai, native apps later. Content should stay end-to-end encrypted.
- Agent tools run without approval today; remote-originated prompts need a security review.
