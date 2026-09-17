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
- Touches the "no Relay account or cloud service" decision (`docs/ROADMAP.md`): reachability from a phone may need P2P with signaling, the user's tailnet, or an optional end-to-end encrypted relay; push notifications on iOS need a server.
- Agent tools run without approval today; remote-originated prompts need a security review.
