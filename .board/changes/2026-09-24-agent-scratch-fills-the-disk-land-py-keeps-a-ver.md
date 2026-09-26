---
id: SZHQ
type: work
status: needs-verification
labels: [bug, disk, land, agents]
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: none, criteria: test_land.py and test_scratch.py pass; land.py verify disk bounded by slot count; relay-scratch check exits 1 over budget; /tmp/claude-1000 shrinks with no live session tree removed, sign_off: none, effort: low, blast: capability}
source: Claude Code pane, 2026-09-24, from the analysis of session 3f4a20ad
links: {plans: [], commits: [8b9410ad, 10ecec2e, 5112bc20, 67083c2f, 01a449fa], evidence: [docs/qa_evidence/2026-09-25-szhq-agent-scratch/], related: [DVV2, 76QW, WNKN], github: null}
---
# Agent scratch fills the disk: land.py keeps a verify build per session forever (155 GB), nothing reclaims /tmp trees, nothing watches

## Issue
clean out those tmp trees and fix that, it will kill others machines that dont have much space. and build skilling / systems for monitoring that. thats something else i really dont like about claude code that we can fix.

## Plan
**Goal.** Agent scratch cannot grow without bound on any machine, and something tells the user before it matters.

**Findings (2026-09-24, this machine).** `/tmp/claude-1000` held 380 GB. `land/` 156 GB, of which 153 GB were per-session `verify/` builds (255 of them, ~0.5-1 GB each, one per land session, never removed; `run_verify` in `scripts/land.py` makes `<root>/<session>/verify`, only `abandon` ever deletes, and nobody runs abandon). Snapshots are 0.45 GB. `-home-elliott-repos-relay-terminal/` (Claude Code's per-session tmp) 112 GB. About 100 GB of ad-hoc agent scratch (`pf4k`, `r-*`, `v-*`, `merge`, ...), days old. No disk check exists anywhere in Relay.

**Steps.**
1. `scripts/land.py`: the verify build moves to a pool of `RELAY_LAND_VERIFY_SLOTS` (default 2) slots under `<root>/verify-slots/<n>`, each held under `flock` for the whole materialise+build; the tree stays incremental because every session's tree is close to the tip. Total verify disk is bounded by the slot count, not the session count.
2. `scripts/land.py`: automatic GC on `begin`, `commit`, `who`, `doctor` and a new `gc` command: legacy per-session `verify/` dirs untouched for 30 min go; session dirs and registry entries idle longer than `RELAY_LAND_GC_DAYS` (default 3) go. `who` prints the root's size.
3. `backend/relay_core/scratch.py` (ships with the backend) + `scripts/relay-scratch`: `report` (what is under the agent scratch roots, by kind, age and whether a live process uses it), `gc [--apply]` (removes entries idle past a threshold and not in use), `check` (exit 1 and one line when scratch exceeds its budget or free space is low).
4. Bundled skill `disk-hygiene`: where scratch goes, cleaning up after yourself, and running `relay-scratch check/gc`.
5. In-app monitor (child agent): the app runs the check periodically and raises a notification with the numbers and a clean-up action.
6. Clean `/tmp/claude-1000` on this machine with the tool from step 3.

**Risks.** Deleting a tree another agent is using right now: GC skips anything a live process has as cwd or open, anything modified in the last N hours, and live land sessions' snapshots.

**Verify.** `tests/test_land.py` gains slot/GC cases; a new `tests/test_scratch.py`; the before/after `du` on this machine.

## Done means
- Twenty land sessions that each run a verify build leave at most `RELAY_LAND_VERIFY_SLOTS` build trees behind, not twenty (a test proves it).
- Stale land session dirs and legacy verify dirs are reclaimed without anyone running `abandon`; a live session's snapshots are never removed.
- `relay-scratch check` exits non-zero with one readable line when agent scratch is over budget or free space is low; `gc --apply` removes only idle, unused entries.
- Agents get a bundled skill that says how to keep scratch bounded; the app surfaces the check to the user.
- This machine's `/tmp/claude-1000` is cleaned (before/after sizes recorded).

## Execution Summary
Landed on `main`:
- `8b9410ad`: `land.py` verify builds run in `RELAY_LAND_VERIFY_SLOTS` (2) flock'd slots per repository under `<root>/verify-slots/`. `gc` reclaims sessions idle over 3 days and pre-pool per-session verify dirs, runs by itself at most hourly from begin/commit/who/doctor, and `who` prints disk use. The default root follows the uid. `CLAUDE.md` updated.
- `10ecec2e`: `backend/relay_core/scratch.py` and `scripts/relay-scratch` (`report`, `gc [--apply]`, `check`).
- `5112bc20`: bundled `disk-hygiene` skill.
- `67083c2f`: the app's monitor, `relay::scratch::Monitor` (`src/ScratchMonitor.{h,cpp}`). It runs `relay-scratch check --json` off the GUI thread a few minutes after launch and then every 6 h, and puts a failing verdict on the bell at most once a day with a Clean up button. `RELAY_SCRATCH_MONITOR=0` disables it. Written by a subagent; I landed it without `src/RelayWindow.h`, whose hunks in that session's diff were #XQ8F's (see #WNKN).
- `01a449fa`: `relay-scratch` installed with the other scripts.

On this machine the automatic `land.py gc` took `/tmp/claude-1000/land` from 156 GB to 4.6 GB, and `/tmp/claude-1000` from 380 GB to ~234 GB. The monitor's commit rebuilt in a warm slot with 1 file refreshed.

**Open:** step 6. The owner stopped the `relay-scratch gc --apply` run (173.7 GB reclaimable at the dry-run), so it waits for their go-ahead. The ledger design that replaces guessing is #DVV2.

## Tests
- `python3 -m unittest tests.test_land`: 72 passed (incl. new `Verify.test_many_sessions_share_a_bounded_pool_of_build_slots`, `…uses_another_slot_while_the_first_is_building`, and 6 `Gc` cases).
- `python3 -m unittest tests.test_scratch`: 12 passed.
- `python3 -m unittest tests.test_skills`: 38 passed (incl. the disk-hygiene case).
- `ctest -R '^(scratchmonitor|notifications)$'`: passed in land.py's build gate on the exact landed tree for `67083c2f`.

## ## Try it
Open: run `bash docs/qa_evidence/2026-09-24-tryit-SZHQ/stage.sh` — it stages a sandbox scratch root (nothing real is touched) holding three agent session trees: 200 MB idle 30 h, 4 KB idle 30 h, and one a live process is sitting in, with the budget forced to 0.1 GB.

Task: run the commands it prints, in order — `relay-scratch check` (note the exit), `relay-scratch gc`, `relay-scratch gc --apply`, then `ls` the tree — and read what each one says and does. (~5 min)

Question: would you let this run on your own machine's real /tmp — did the one line from `check` and the removal list from `gc` tell you enough to trust it with the real thing?

The expected result is sealed in `docs/qa_evidence/2026-09-24-tryit-SZHQ/expected.md` until you have answered.
