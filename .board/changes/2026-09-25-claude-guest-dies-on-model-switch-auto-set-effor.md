---
id: PMZZ
type: work
status: needs-verification
labels: [bug, guest]
assignee: agent
implemented_by: kimi/k3
session: 22847335-dbb1-4f65-838e-f1e23b1823b5
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [probe], human: none, sign_off: none, effort: low}
links: {plans: [], commits: [0e52c2ddde75], evidence: [docs/qa_evidence/2026-09-25-pmzz-claude-guest-relaunch/], related: [], github: null}
---
# Claude guest dies on model switch + auto set_effort relaunch ("Session ID already in use"), leaving "the guest is not running."

## Issue
Switching a restored claude-guest pane from a hosted model (kimi k3) to opus resumes the claude session, then the automatic effort relaunch kills it: `_relaunch` picks `--session-id` over `--resume` because no turn has run yet, claude refuses the id ("Session ID … is already in use"), and the pane is left with a dead harness that answers every later prompt with "the guest is not running."

> can you check what happened here with the claude guest? 22f05421 — when i opened the pane, it was in kimi, i changed to opus and tried to resume, and this is the error i got: ✗ the guest is not running.
> — elliott · [session:8fbcd69d33b64756ab3391de76534215](relay://session/8fbcd69d33b64756ab3391de76534215) · 2026-09-25

## Planning notes
**Root cause (two defects, from `worker.log` 2026-09-25 19:15, pane 22f05421, run e53411ae):**

1. `_relaunch` mis-decides `--resume` vs `--session-id` (`backend/relay_core/guest_harness_claude.py:963`). It resumes only when `self._resumable`, which is set when the CLI's `init` system message arrives (`:761`) — i.e. after the first turn. A harness just started with `start(resume=X)` has run no turn, so `_resumable` is False even though session X has a transcript on disk (it is what we resumed from). The relaunch then spawns `claude --session-id X` against an existing session and the CLI exits at once: `Error: Session ID X is already in use.` `_handshake` fails → `HarnessNotAvailable("Claude Code did not start: the guest stopped.")` → harness closed, `_proc = None`.
2. No recovery: the pane's provider keeps the dead harness. `send()` raises `the guest is not running.` (`:567`) on every later prompt instead of relaunching; `set_model`/`set_effort` raise the same (`:944`/`:1005`) — pane 51f880d9 at 16:32 today hit that route (`Model switch failed: the guest is not running.`) after an earlier failed relaunch.

**Sequence (worker.log, pane 22f05421):**

- 19:15:05 pane restored and configured `model=k3 host=api.kimi.ai` (what the user saw as "in kimi"; the pane's saved preset had become kimi after the earlier OOM kill of the session).
- 19:15:40 user picks opus → configure preset `guest:claude`, harness A starts `--resume 922fd7e8`, handshake OK.
- 19:15:41 the model pick also carries effort `medium` → `set_effort` → claude changes effort only by relaunch → `_relaunch` picks `--session-id 922fd7e8` (bug 1) → claude: `Session ID 922fd7e8 is already in use` → `protocol_error kind=set_effort "Claude Code did not start: the guest stopped."`, harness dead.
- 19:15:43 and 19:15:59 the two prompt submits → `turn_end outcome=error error="the guest is not running."` (5 ms, no tools) — bug 2.
- 19:16:07 user gives up, switches to glm-5.3; `guest_harness_closed`.

Reproduced twice today on two panes (22f05421 and 51f880d9), on two builds; the relaunch logic came with #GT7X (commit 91c8cf25).

**Fix sketch:**

- In `start()`, seed `_resumable = bool(resume)` (a resumed session has a transcript by definition), so `_relaunch` uses `--resume`. Keep the init-message assignment as is.
- Recovery: `send()`/`set_model`/`set_effort` on a closed harness should relaunch it (same session id) instead of raising — or at minimum the turn error should say to restart, not leave the pane stuck.

**Evidence:** `~/.local/share/relay/logs/worker.log` (2026-09-25, pane 22f05421, 19:15:40–19:16:07; pane 51f880d9 at 16:32), scrollback `22f05421-86e1-4623-b79d-415f3b71dd47`.

## Done means
- A harness started with `start(resume=X)` that has run no turn yet, when asked to change model or effort (`_relaunch`), spawns the CLI with `--resume X`, not `--session-id X` — claude no longer refuses with "Session ID … is already in use".
- `send()`, `set_model()` and `set_effort()` on a harness whose process is gone (`_proc is None`) relaunch it on the same session and proceed, instead of leaving the pane permanently erroring "the guest is not running.".
- Failure would show as the old behaviour in the targeted tests: a relaunch argv carrying `--session-id` for a resumed-but-idle session, or a raise of `the guest is not running.` after `close()`.

## Execution Summary
- `start()` seeds `_resumable` from the resume it was given (a resumed session has a transcript; claude exits at start on one it cannot read), so the first relaunch — the effort change that rides a model pick — uses `--resume <id>` instead of `--session-id <id>` (commit 0e52c2ddde75).
- `send()` on a closed harness relaunches it on the session it had (same model/effort) instead of raising `the guest is not running.`; `set_model()` restarts on the new model; `set_effort()` records the level for the start the next `send()` does.
- A harness never started (`_session_id`/`_cwd` empty) still raises, so those errors keep meaning what they meant.

## Tests
- `PYTHONPATH=backend python3 -m pytest tests/test_guest_harness_claude.py -q` — 83 passed (78 before + 5 new: the resumed-before-first-turn argv for both `set_model` and `set_effort`, `set_model` on a closed harness, `set_effort` on a closed harness, `send` on a closed harness).
- The five new tests were run against the pre-fix snapshot (`land.py` snap of `guest_harness_claude.py`): 5 failed — two on the `--session-id`-instead-of-`--resume` decision, three on `HarnessError: the guest is not running.` — the exact 19:15 pane behaviour.
- `PYTHONPATH=backend python3 -m pytest tests/test_guest_harness_provider.py tests/test_guest_harness_codex.py tests/test_guest_harness_steer.py -q` — 170 passed (neighbouring harness suites untouched).
- Evidence: `docs/qa_evidence/2026-09-25-pmzz-claude-guest-relaunch/`.
