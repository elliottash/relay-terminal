---
id: C8Z7
type: work
status: needs-verification
labels: [bug, switchboard]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: a573bf09-792c-489e-9ed6-362ece76f759
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
source: claude pane (FKSN run), 2026-09-25
links: {plans: [], commits: [7bef1b88a656], evidence: [docs/qa_evidence/2026-09-25-land-gate-cwd/], related: [FYEY, FKSN], github: null}
---
# Land gate runs land.py status from the worker's cwd, so it fails outside the repo and silently skips the uncommitted-work check

## Issue
Moving #FKSN to needs-verification through the guest bridge returned `land_warning: "land status failed: land.py: not inside a git repository: /home/elliott; uncommitted work was not checked"`. `_land_gate` (`backend/relay_core/board_tools.py` ~4196) calls `subprocess.run([sys.executable, land, "status", "--token", …, "--json"])` without `cwd=`, so land.py resolves the repo from whatever directory the worker process is in. Here that was `/home/elliott`. The gate fails open, so the #FYEY uncommitted-work check never ran for that move, and the only sign is a warning the agent can easily miss. Likely fix: pass `cwd=str(self.board.repo)` (or land.py's `--repo`/`-C`, if it has one), and add a test that runs the gate from an unrelated cwd. Also consider whether a failed status should say so in the thread, not only in the tool result.

> write a card on that bard notification about /home/elliott, that seems like a bug
> — elliott · [session:cb88079527834faf94da87ccb60a5ba5](relay://session/cb88079527834faf94da87ccb60a5ba5) · 2026-09-25

## Done means
- Moving a card to `needs-verification` or `done` checks its pane's uncommitted work in the board repository, even when the worker starts outside Git or inside a different repository.
- Uncommitted hunks refuse the move and name the files; a clean report permits it without a cwd-related warning.
- A genuine status failure keeps the existing fail-open behavior but records that work was not checked in both `land_warning` and the card's move event in its thread.
- Failure is a move that bypasses dirty claims because of the worker's cwd, checks the wrong repository, or loses a status-failure warning from the shared record.

## Plan
**Goal.** Make the land gate check the board repository regardless of the worker's current directory, and preserve any failed-check warning in the card's shared thread.

**Findings.**
- `backend/relay_core/board_tools.py`: `BoardTools._land_gate` launches the board repository's `scripts/land.py status --token … --json` without `cwd`. `scripts/land.py:repo_root` defaults to `os.getcwd()`, explaining the `/home/elliott` failure reported by #FKSN.
- `BoardTools._move` calls the gate before saving the move, returns its warning as `land_warning`, but omits it from the move event appended with `_append`.
- `tests/test_board_tools.py:LandGateTests` covers dirty/clean reports, script failure, and skip cases. Its fake scripts ignore cwd, so they cannot catch this bug. #FYEY explicitly implemented warning-only behavior for a failing/slow status command.

**Steps.**
1. In `_land_gate`, pass `cwd=str(self.board.repo)` to `subprocess.run`. Keep the pane token, timeout, and skip conditions unchanged; do not change the worker process's cwd.
2. In `_move`, append a nonempty `land_note` to the existing move-event text before `_append`, retaining the `land_warning` result field. Record it once, within the existing move/undo bookkeeping; clean and skipped checks add nothing.
3. Extend `LandGateTests` with a cwd-sensitive child script that fails unless its cwd is the fixture's board repository. Exercise both target statuses from an unrelated non-Git directory and a different temporary Git repository, for dirty and clean reports. Dirty reports must refuse without changing status; clean reports must succeed without warnings. Restore the test process cwd with guaranteed cleanup.
4. Extend failure tests to assert the warning appears in both the result and the existing move event, exactly once. Cover nonzero exit, timeout, launch error, and unreadable JSON (mock timeout/error instead of waiting). Retain existing skip coverage and add the same-status skip case; assert clean/skipped moves have no failure note.

**Risks.** Preserve #FYEY's deliberate fail-open policy: this fix makes failures visible but does not make them block moves. Test cwd changes are process-global and need reliable restoration. Keep fixtures and land state isolated from real pane claims. No owner decision is required for this scope.

**Verify.** Run `PYTHONPATH=backend python3 -m pytest tests/test_board_tools.py -k LandGateTests`, then the complete `tests/test_board_tools.py` file. Demonstrate that the cwd-sensitive regressions fail before the fix and pass afterward; inspect the persisted thread for a simulated failed status and confirm a dirty report leaves the card unchanged. Verification proposal: artifact `code`, primary `script`, human `none`, effort `low`. No C++ build is needed.

## Execution Summary
Commit `7bef1b88`.
- `_land_gate` (`backend/relay_core/board_tools.py`) now runs `scripts/land.py status` with `cwd=str(self.board.repo)`, so the worker's own cwd (outside Git, or another repo) no longer decides which repository is checked. Token, timeout and skip conditions unchanged; fail-open policy from #FYEY kept.
- `_move` appends a non-empty `land_note` to the move event line, so a status that could not answer is recorded once in the card's thread as well as returned as `land_warning`. Clean and skipped checks add nothing.

## Tests
- `PYTHONPATH=backend python3 -m pytest tests/test_board_tools.py -k LandGateTests` — 11 passed (4 new tests: cwd-sensitive fake land.py from a non-Git dir and another Git repo for `needs-verification` and `done`, dirty refuses and leaves status, clean moves with no warning; warning recorded exactly once in the move event for nonzero exit, unreadable JSON, timeout and launch error; clean/same-status moves add no note).
- Same 4 tests fail against a scratch backend copy with the fix removed (`land-gate-tests.txt`): the dirty move from an unrelated cwd went through with a warning.
- Real `scripts/land.py status` from `/home/elliott` reproduces the #FKSN error and answers with `cwd=<repo>` (`real-land-status.txt`).
- Full `tests/test_board_tools.py`: 340 passed.
