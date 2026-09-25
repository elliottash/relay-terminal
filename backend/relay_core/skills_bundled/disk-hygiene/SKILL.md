---
name: disk-hygiene
description: Keep agent scratch (tree copies, build dirs, test workspaces) from filling the disk; check, report and safely clean what agents left in /tmp.
short: Keep agent scratch bounded and clean up what agents left in /tmp.
---

# Disk hygiene for agents

Agents fill disks quietly. Every "let me export the tree to /tmp and build it there" leaves a
source copy and a build directory behind — often a gigabyte each — and nobody removes them. On
2026-09-24 one machine had **380 GB** of it in `/tmp/claude-<uid>/`: 153 GB of per-session verify
builds, 112 GB of per-session tool output, and about 100 GB of ad-hoc exports named after cards.
On a laptop that is the whole disk, and the user finds out when something unrelated fails.

**Find Relay's scripts once.** `scripts/relay-scratch` is relative to a Relay checkout; otherwise
look in `/usr/share/relay/scripts/` or `/usr/local/share/relay/scripts/` (inside Relay panes it is
on `PATH`). `python3 -m relay_core.scratch` does the same where the backend is importable.

## Rules while you work

1. **Do not copy the repository to build or test it.** Build in the checkout's own build directory
   through the project's wrapper, or in a tool that already owns a bounded place for it (in the
   Relay repo, `scripts/land.py` builds the exact tree in a shared pool of slots). A second full
   build directory is the most expensive thing you can leave behind.
2. **If you must make scratch, make it removable.** Put it under `$TMPDIR/claude-<uid>/<card or
   session>/`, one directory per task, so it is one entry to measure and remove. Prefer
   `tempfile.TemporaryDirectory()` / `mktemp -d` with a `trap 'rm -rf "$dir"' EXIT` over a named
   directory you mean to delete later.
3. **Remove it when the task ends**, in the same turn you stop needing it — after the evidence you
   want to keep is copied into the repository (for example `docs/qa_evidence/`). Say in your report
   what you removed.
4. **Never delete what you did not make** unless the user asked: another session may be using it.
   `relay-scratch gc` exists for that and checks for live users first.
5. **Before a large build or export, look at free space**: `relay-scratch check`. If it fails, tell
   the user the line it printed instead of starting a build that will die half-way.

## Commands

```sh
relay-scratch                 # report: every entry, biggest first, kind, idle hours, why kept
relay-scratch check           # exit 1 and one line when scratch is over budget or disk is low
relay-scratch gc              # dry run: what would go
relay-scratch gc --apply      # remove entries idle > 24h that no live process uses
relay-scratch --idle-hours 72 gc --apply    # more conservative
python3 scripts/land.py gc --dry-run        # Relay repo: land.py's own snapshots and slots
```

`gc` only removes entries owned by this user, never follows a symlink, never touches an entry a
live process has as its working directory, holds open, or names on its command line, never one
written in the last `--idle-hours`, and leaves `land/` to `land.py gc` (which knows which
snapshots are live). It also lists the shared compiler cache (`~/.cache/relay/ccache`, kind
`compiler-cache`) and counts it against the budget, but never removes it: ccache evicts its own
objects at the cap in its `ccache.conf` (`RELAY_CCACHE_MAX`, default 10G). To empty it on purpose,
`CCACHE_DIR=~/.cache/relay/ccache ccache -C`. Budget and floor: `RELAY_SCRATCH_BUDGET_GB` (default the smaller of 20 GB and
5% of the disk) and `RELAY_SCRATCH_MIN_FREE_GB` (default the larger of 5 GB and 5%).

The Relay app runs the same check itself, a few minutes after launch and then every six hours, and
puts a failing verdict on the notification bell (at most once a day) with a **Clean up** button that
runs `gc --apply`. `RELAY_SCRATCH_MONITOR=0` turns that off. A user who mentions that notice is
asking about this: follow "When the user asks to clean up" below.

## When the user asks to clean up

1. Run `relay-scratch` and show the top of the report and the total.
2. Run `relay-scratch gc` (dry run) and say how much would be freed and what is kept and why —
   particularly anything kept because a live process uses it.
3. Only with their go-ahead, `relay-scratch gc --apply`; then `relay-scratch check` and report the
   before and after.
