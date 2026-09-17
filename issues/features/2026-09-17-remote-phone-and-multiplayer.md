# Remote access from a phone and multiplayer shared terminals

- **Status**: open
- **Component**: gui, worker, terminal engine
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: from a phone, the owner follows and drives a desktop Relay pane's agent and terminal with notifications; two people share a pane with clear control handoff; all traffic end-to-end encrypted
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, 2026-09-17: "another important feature i need: remote access on phone. multiplayer shared terminals. i like warp remote control and blink but they kind of suck. lets make a good version of that."

## Status

Research and design in progress: `docs/REMOTE-AND-MULTIPLAYER-DESIGN.md` (pending).

## Known constraints

- Depends on Relay's own engine (`docs/ENGINE.md`): KonsolePart cannot serialize screen state.
- Owner (2026-09-17): "the no account / cloud / etc are not strict constraints. ideally it could be done in a browser on relay-terminal.ai as a first version, later on we make android / phone apps." A Relay-operated service and accounts are acceptable; v1 is a browser client on relay-terminal.ai, native apps later. Content should stay end-to-end encrypted.
- Agent tools run without approval today; remote-originated prompts need a security review.
