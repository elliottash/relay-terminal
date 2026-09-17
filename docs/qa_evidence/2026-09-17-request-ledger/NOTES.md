# Implementer evidence: request ledger, todos, completion check (2026-09-17)

Implementer: Claude Opus 5 (Claude Code). These are implementer runs, not QA verdicts.

## Automated

- `test-output.txt`: `./scripts/test.sh`, 272 tests OK (233 before; 39 new in `tests/test_requests.py`).
- `build-output.txt`: `cmake --build build` succeeds; `ctest --test-dir build`: 5/5 passed.
- Tests use stub providers only; `route_assist.router_provider` and `keystore.lookup` are patched to fail if used.

## Live (`scripts/eval-requests.py`, preset `openrouter`, DeepSeek V4.1 Flash, todo tool on, one run each)

Transcripts are worker events without deltas (`*.jsonl`); summaries hold ledger and todos. No keys in any file
(checked with grep for `sk-or-` and `Bearer`).

| Scenario | Result | Notes |
|---|---|---|
| 1: five numbered asks | 5/5 files | 5 todos, all linked to R1, all completed; no completion check needed |
| 2: "fix X, and also rename Y, oh and update the README" | 3/3 | 3 todos |
| 3: 3 steers during a 12 s command | 4/4 (run twice: `live/`, `live-rerun/`) | steers R2–R4 each got its own todo; all `done` |
| 4: interrupt with a new ask, then "continue the earlier one" | 2/2 | first run (`live/`): model cancelled R1's todos during the interrupt turn, citing "user did not ask to resume it", so R1 ended `cancelled` although the work was done later under R3. Fixed by (a) todos of other requests no longer holding a turn open and (b) prompt rule "todos of an earlier stopped request may stay pending". Rerun (`live-rerun/`): R1's todos reused and R1 `done` |
| 6: 16K window, constraint at turn 1, 9 queued asks | 9/9, constraint kept (tests/ untouched) | 1 auto compaction 13,159 → 5,551 tokens, carried block 2,042 chars (9 requests, 2 todos, 7 recent user messages); one completion check fired (open todo "show summary") and was resolved |

Not done: 3 runs per preset, other presets, with/without todo tool comparison (research section 7 eval matrix).
