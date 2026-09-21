---
id: QG4C
type: work
status: discussing
labels: [bug, guest, terminal]
assignee: claude-code
waiting_on: owner
rank: m
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# The command queue does not work with claude / codex

## Issue
the command queue doesnt seem to work with claude / codex

## Discussion points
Two different surfaces answer to "claude / codex", and the queue is a different mechanism in each:

- **A TUI guest in the pane** (§26.8: `claude` or `codex` running as the foreground program, the
  Relay composer typing into it). Here a submitted line is typed into the guest when it waits at
  its input and held in the pane's queue while `guest_busy`.
- **A guest harness preset** (§29, `guest:claude` / `guest:codex` as the pane's own model, which is
  what the model box now picks). Here a queued prompt is an ordinary agent entry and waits on
  `agent_finished` like any provider's.

The GUI log for this evening shows only harness panes in Relay (`preset=guest:claude`,
`guest_in_front=` empty), and the three TUI `claude`/`codex` processes on the machine belong to
Warp, not to Relay — so the reproduction matters before anything is changed.
