---
name: disk-hygiene
description: Keep agent scratch (tree copies, build dirs, test workspaces) bounded; Relay owns every temp dir through the scratch ledger — ask for it, and release it.
short: Relay owns scratch: ask for temp dirs and release them; keep the disk bounded.
---

# Disk hygiene for agents

Agents fill disks quietly. Every "let me export the tree to /tmp and build it there" leaves a
source copy and a build directory behind — often a gigabyte each — and nobody removes them. On
2026-09-24 one machine had **380 GB** of it in `/tmp/claude-<uid>/`: 153 GB of per-session verify
builds, 112 GB of per-session tool output, and about 100 GB of ad-hoc exports named after cards.
On a laptop that is the whole disk, and the user finds out when something unrelated fails. Worse,
/tmp is wiped at reboot, so the drafts that *did* matter died there too.

So Relay owns scratch now (card #DVV2). **You never pick a temp path.** You ask, Relay decides
where it lives, records one ledger row for it, and ends it. Anything that must outlive the task is
not scratch — it goes in the project and is promoted there.

**Find Relay's scripts once.** `scripts/relay-scratch` is relative to a Relay checkout; otherwise
look in `/usr/share/relay/scripts/` or `/usr/local/share/relay/scripts/` (inside Relay panes it is
on `PATH`). `python3 -m relay_core.scratch` does the same where the backend is importable.

## Rules while you work

1. **Ask for scratch; never invent a path.** Use the `scratch_dir` tool (or, in a shell,
   `relay-scratch new --class scratch --purpose "what it is for" --card <id>`), which prints the
   path. `TMPDIR` already points at this session's ledgered scratch root, so `mktemp`,
   `mktemp -d` and Python's `tempfile` land somewhere owned without you doing anything. Never
   write to a bare `/tmp/...` path of your own choosing and never create a new top-level folder
   in `$HOME` — the tool layer refuses, and the post-turn sweep asks about anything that slipped
   through (ledger it, move it into the project, or delete it).
2. **Three classes, three homes.** `scratch` (disposable: exports, build dirs, test workspaces)
   lives under `$XDG_CACHE_HOME/relay/scratch/<session>/`; `keep` (output the work needs later:
   evidence drafts, reports, message drafts) is created with `--class keep` inside the project at
   `<project>/.relay/work/` (git-ignored) and promoted into the repo (`docs/qa_evidence/…`) when
   it is final — never left in /tmp; `install` (a tool you install for the user) goes under
   `$XDG_DATA_HOME/relay/tools/<name>/`, never a new top-level folder in `$HOME`.
3. **Do not copy the repository to build or test it.** Build in the checkout's own build directory
   through the project's wrapper, or in a tool that already owns a bounded place for it (in the
   Relay repo, `scripts/land.py` builds the exact tree in a shared pool of slots). A second full
   build directory is the most expensive thing you can leave behind.
4. **Release it when the task ends**, in the same turn you stop needing it — `scratch_release`,
   or `relay-scratch release <id-or-path>`; `keep` dirs are released by promoting
   (`--promote-to docs/qa_evidence/...`) or dropping (`--drop`) explicitly, never silently. Say in
   your report what you released. When a session closes, its scratch is reclaimed automatically.
5. **Never delete what you did not make** unless the user asked: another session may be using it.
   `relay-scratch gc` exists for that and checks for live users first.
6. **Before a large build or export, look at free space**: `relay-scratch check`. If it fails, tell
   the user the line it printed instead of starting a build that will die half-way.

## Commands

```sh
relay-scratch new --class scratch --purpose "export tree for card X"   # prints a ledgered path
relay-scratch release <id-or-path>            # scratch: reclaimed; keep: --promote-to or --drop
relay-scratch                                 # report: every entry, biggest first, kind, idle, why kept
relay-scratch ledger                          # the ledger: totals by class and session, orphans
relay-scratch check                           # exit 1 and one line when scratch is over budget or disk is low
relay-scratch gc                              # dry run: what would go
relay-scratch gc --apply                      # remove entries idle > 24h that no live process uses
relay-scratch --idle-hours 72 gc --apply      # more conservative
python3 scripts/land.py gc --dry-run          # Relay repo: land.py's own snapshots and slots
```

`gc --apply` refuses to run below a 6-hour idle floor on the default roots without `--force-idle`
— an agent's "quick clean" once deleted other sessions' live scratch in one line. `gc` only
removes entries owned by this user, never follows a symlink, never touches an entry a live process
has as its working directory, holds open, or names on its command line, never one written in the
last `--idle-hours`, and leaves `land/` to `land.py gc` (which knows which snapshots are live).
Every removal is a ledger transition (`reclaimed`, with the size freed); `keep` rows are never
auto-deleted and `install` rows stay until the user removes them. The ledger itself lives at
`$XDG_STATE_HOME/relay/scratch-ledger.jsonl`. It also lists the shared compiler cache
(`~/.cache/relay/ccache`, kind `compiler-cache`) and counts it against the budget, but never
removes it: ccache evicts its own objects at the cap in its `ccache.conf` (`RELAY_CCACHE_MAX`,
default 10G). To empty it on purpose, `CCACHE_DIR=~/.cache/relay/ccache ccache -C`. Budget and
floor: `RELAY_SCRATCH_BUDGET_GB` (default the smaller of 20 GB and 5% of the disk) and
`RELAY_SCRATCH_MIN_FREE_GB` (default the larger of 5 GB and 5%).

The Relay app runs the same check itself, a few minutes after launch and then every six hours, and
puts a failing verdict on the notification bell (at most once a day) with a **Clean up** button that
runs `gc --apply`. `RELAY_SCRATCH_MONITOR=0` turns that off. A user who mentions that notice is
asking about this: follow "When the user asks to clean up" below.

## When the user asks to clean up

1. Run `relay-scratch` and show the top of the report and the total, plus `relay-scratch ledger`
   for what is live by class and session (this often shows a `keep` dir nobody promoted, or
   orphans from before the ledger existed — `relay-scratch adopt` lists those for a keep / promote
   / reclaim decision; it deletes nothing by itself).
2. Run `relay-scratch gc` (dry run) and say how much would be freed and what is kept and why —
   particularly anything kept because a live process uses it.
3. Only with their go-ahead, `relay-scratch gc --apply`; then `relay-scratch check` and report the
   before and after.
