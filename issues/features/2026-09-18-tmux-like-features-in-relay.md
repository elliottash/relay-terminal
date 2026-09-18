---
id: 87HB
type: work
status: inbox
labels: [feature, design]
component: [gui, shell-integration]
milestone: desktop-alpha
workstream: terminal
rank: zzzzzm
created: '2026-09-18'
source: owner, in a Claude Code session, 2026-09-18, while deciding how #S5SH should behave inside tmux
links: {plans: ['docs/SSH-AND-MOSH.md'], commits: [], evidence: [], related: ['S5SH', 'SPBN'], github: null}
---
# The parts of tmux worth having in Relay itself

## Issue

> do the small fix, i dont use tmux that often, but other users will. something i would also like to
> file as a feature request, is if there are some tmux like features we can implement directly
> through relay terminal.

(The "small fix" is #S5SH's: Relay's remote integration wraps its escape sequences so they survive a
tmux on the host.)

## What tmux is actually used for, and where Relay stands

| What people use tmux for | Relay today |
|---|---|
| Splits and tabs | Has them, natively, with the mouse and the keyboard. Nothing to add. |
| Scrollback, search, copy mode | Has them, with clickable paths and folds on top. |
| Naming and switching sessions | Tabs and panes have titles; conversations have their own list. |
| **A shell that survives the client dying** (network drop, crash, quit, reboot of the laptop but not the host) | **Missing.** A pane's shell is a child of Relay: when Relay exits or crashes, every running command dies with it. The layout, the directory and the scrollback text come back (`src/WindowState.h`); the processes do not. |
| **Detach here, attach there** | Partly: a pane can be shared with a paired phone (`remote/`), but there is no "leave it running and pick it up from another machine". |
| **On a remote host: keep a long job running across a disconnect** | Missing, and it is the reason most people type `tmux` after `ssh` at all. mosh (#S5SH) survives a *network* drop but not a client restart. |
| Pair programming in one session | The phone share covers watching; two desktops do not share a pane. |

So the gap is one thing, in two places: **a shell that outlives the window**, locally and on the host.

## Options

1. **A local session keeper.** Relay stops owning the pty directly: each pane's shell is started by a
   small helper (one process per pane, or one daemon for all of them) that owns the pty and keeps a
   ring buffer of output. Relay attaches over a Unix socket in `$XDG_RUNTIME_DIR`. Quitting Relay
   leaves the helper running (by choice, per pane or per tab); starting Relay offers to re-attach,
   with the live processes still there. A crash becomes recoverable instead of destructive.
   - Fits what is already built: `engine/pty/` is where the pty lives, the layout restore already
     knows which panes existed, and the scrollback restore already replays text.
   - Costs: a second process to ship and version, a protocol between it and the GUI, and careful
     handling of the terminal size and of orphaned helpers.
2. **The same keeper on the host.** With #S5SH the pane knows which host it is on and can run
   commands there over the user's own connection. The keeper binary could be copied to the host once
   (like Warp's and VS Code's remote servers) so a remote shell survives a disconnect without tmux.
   Bigger: per-architecture builds, versions, and a decision about installing anything on a host.
3. **Use an existing keeper instead of writing one**: `abduco`, `dtach`, or tmux itself in a single
   session, hidden behind Relay's own UI. Least code, and it works on hosts that already have tmux;
   but it is a dependency the user must have, and Warp's tmux experiment is a warning about hiding
   one behind your own UI (docs/SSH-AND-MOSH.md, "Research notes").
4. **`tmux -CC` control mode**: talk tmux's control protocol and map its windows and panes onto
   Relay's splits, the way iTerm2 does. Gives persistence and remote persistence with no new binary,
   but it is a large state machine and only helps people who already run tmux.

A sensible order is 1, then 2 or 4 once 1 has proved the attach/detach model. 3 is the cheap
experiment if the owner wants it working this week on hosts that have tmux.

## Open questions for the owner

- Is "quit Relay, come back, the build is still running" worth a helper process in the product?
- Should re-attaching be automatic, or offered ("3 panes are still running")?
- Does the remote case (option 2) matter, or is mosh plus a long-running job with `nohup` enough?
