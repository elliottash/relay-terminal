---
id: S5SH
type: work
status: in-progress
labels: [feature]
component: [gui, shell-integration, agent, router]
milestone: desktop-alpha
workstream: terminal
rank: zzzzzk
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session relay-terminal-be), 2026-09-18
created: '2026-09-18'
acceptance: in a recorded run against a real host, the prompt box types a command into the ssh session, the agent's reply at the remote prompt prints into the terminal, a remote sudo prompt masks the prompt box, and the agent runs a command on the host over the user's connection without a second login
source: owner, in a Claude Code session, 2026-09-18, after the agent's reply went to the side panel while ssh sat at a remote prompt
links: {plans: ['docs/SSH-AND-MOSH.md'], commits: [], evidence: [], related: ['SPBN', 'D8J3', 'C1HH'], github: null}
---
# SSH and mosh sessions: the prompt box, the agent and the reply work on the remote host

## Issue

> i just observed this issue: [screenshot: "Agent · glm-5.3 — output will also print in the
> terminal when ssh exits"] when i pressed enter twice to interrupt with a command, it started
> replying in the thinking bubble, instead of in the terminal

> do research on how warp terminal deals with ssh sessions, including for example looking at their
> github. see if there are other AI harnesses or terminals that could have helpful inputs. then
> help me build a nice set of ssh / mosh features in relay

## Decisions

Owner, 2026-09-18, asked in the session:

- Remote shell integration: "automatic wrapper by default, but you can disable that in the options
  menu (offer per host, or off)".
- The agent running commands on the ssh host over the user's connection: "Yes, always".
- The agent's reply while ssh sits at a remote prompt: into the terminal.

## Tasks

- [x] Wrapper: `ssh()`/`mosh()` add connection sharing in Relay pane shells (`shell/integration.bash`)
- [x] Remote integration script, typed once per login (`shell/remote-integration.sh`)
- [ ] Engine keeps OSC 7's host; a remote `cd` no longer moves the local cwd
- [ ] Pane: remote session model from `ssh -G`, remote prompt detection
- [ ] Prompt box types commands into the remote shell; router `remote` flag
  - [x] Router half: `route {remote: {host}}` decides shell vs agent by shape, never "not found" locally; `remote_host` on the decision (`backend/relay_core/router.py`, protocol section 24.1)
- [ ] Remote password prompts mask the prompt box
- [ ] Agent reply prints into the terminal at a remote prompt
- [x] Agent context `remote_session`; `run_command` `host` over the shared connection (`backend/relay_core/remote_session.py`, protocol section 24.2–24.3; the GUI still has to send it)
- [ ] Connect to host (palette, from `~/.ssh/config`), split on the same host
- [ ] Options › Terminal › SSH sessions: auto / ask / off
- [ ] Clickable paths in a remote pane do not open local files

Design: [`docs/SSH-AND-MOSH.md`](../../docs/SSH-AND-MOSH.md).
