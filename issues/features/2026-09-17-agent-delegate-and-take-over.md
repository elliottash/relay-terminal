---
id: C1HH
type: work
status: ready
component: [agent, gui, shell-integration]
milestone: desktop-alpha
workstream: agent
rank: 0k
created: '2026-09-17'
acceptance: a recorded run where the agent edits a file in vim in the visible pane, the user takes over mid-session with a keystroke, and hands control back
source: '`issues/feature_intake.txt`, "check that i can run programs, eg nano / vim. i need the delegate / take over functionality like warp."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Agent drives interactive programs in the visible pane, with take-over

## Owner decision (2026-09-17)

The agent drives the user's **visible** pane, not a separate hidden pane.

## Current state

- Interactive programs work for the user: on 2026-09-17 vim opened from the composer, accepted
  input, and `:wq` saved the file (Xvfb check).
- The same check found Relay's window shortcuts steal keys from TUIs: Ctrl+W inside vim opened
  the close-window dialog. Tracked with the keyboard pass-through work.
- The agent's `run_command` uses a separate non-interactive Bash process. It cannot see or type
  into a pane.

## What is needed

1. **Input:** a tool to send keystrokes to the pane (`TerminalInterface::sendInput`). Easy.
2. **Screen reading:** Konsole 23.08 exposes no screen-text API to KonsolePart hosts (verified by
   probing the Session and SessionAdaptor methods). Options: run each shell through a Relay PTY
   proxy that mirrors the screen in a VT emulator; run panes inside a hidden tmux session and use
   `capture-pane` / `send-keys`; or use a newer Konsole API on KF6 if one exists. Needs a spike.
3. **Control handoff:** a visible "agent in control" indicator on the pane; any user keystroke
   takes over and pauses the agent; an explicit "hand back" action.
4. **Safety:** tools run without approval, so the agent typing into a live shell has a large
   blast radius. Stop agent must halt keystroke injection immediately.

## Next step

A spike comparing the PTY-proxy and tmux approaches for screen fidelity, latency, scrollback,
mouse support and interference with the user's own tmux.
