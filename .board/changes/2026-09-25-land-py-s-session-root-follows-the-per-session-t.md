---
id: BHJZ
type: work
status: needs-verification
labels: [bug, land, scratch]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: d0d996b2-de76-404a-8903-c38c30208f92
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: 'Claude Code guest session in Relay, 2026-09-25, while planning #P2W8''s subagent waves'
links: {plans: [], commits: [37d660ae174d, aaecaa7c3fda], evidence: [docs/qa_evidence/2026-09-25-bhjz-shared-land-root/], related: [DVV2, P2W8], github: null}
---
# land.py's session root follows the per-session TMPDIR, so `who` and the contested-hunk check cannot see other sessions

## Issue
i want to go big and build the whole thing ... then delegate to subagents.

## Done means
- Two processes with different `TMPDIR`s and no `--root`/`RELAY_LAND_ROOT` share one land root: a `begin` in one shows up in the other's `who`, and a claim by one is warned about / contested in the other. Failure looks like `who` in a Relay pane saying "no land sessions" while another pane holds a claim.
- `who` always prints the root it read.
- A session that already has snapshots in its old per-TMPDIR root can still `commit`/`abandon` (it is told where they were found) instead of being refused as "edited without begin".
- `tests/test_land.py` covers the TMPDIR-independence and the legacy fallback.

## Tests
- `python3 -m unittest tests.test_land` — 74 pass. New `SharedRoot` class: `test_default_root_ignores_tmpdir` (two TMPDIRs, no `--root`/`RELAY_LAND_ROOT` → same default root, not under either TMPDIR) and `test_session_begun_under_old_tmpdir_root_can_still_commit` (begin under `<TMPDIR>/claude-<uid>/land`, commit with no `--root` lands and stderr names the old root). Both fail against `HEAD~1`'s land.py (run via `RELAY_LAND_SCRIPT`).
- Live: `TMPDIR=<a Relay pane's scratch tmp> python3 scripts/land.py who` now reads `/tmp/claude-1000/land` and lists session `bhjz`, begun from a shell with no TMPDIR (`docs/qa_evidence/2026-09-25-bhjz-shared-land-root/who-and-tests.txt`).
