# Collection identity and recovery — #HG26, related #AQ6X

Implemented runnable `collection:unittest:<module>` identities in the JUnit runner/history,
synthetic-row deduplication within each run, and scope-aware reruns. `_FailedTest` is never
used as a runnable class. Complete module executions or discovery report scope outcomes;
a method/class subset cannot emit a module pass. A failing full-scope execution resets the
recovery streak. Ordinary test failures do not independently create collection incidents.

Historical `_FailedTest` keys retain their identity, events and claims. Their old run container
is preserved so a previously owned incident cannot disappear on upgrade. Explicit same-module
outcomes answer both historical records; two consecutive passing executions resolve them.
Historical green rows without scope metadata are deliberately not inferred to be full collection.
The legacy short-module spelling maps to `tests.<name>`, matching this project's old discovery.

Implementation commit: `9bef2478f4d4071f520d9728cd5b90f88daa31c0`.

## Verification

`PYTHONPATH=backend:tests python3 -m unittest tests.test_collection_recovery tests.test_junit_runner tests.test_test_history tests.test_signals tests.test_tests_protocol -q`

269 targeted tests passed. New subprocess regressions cover repeated imports, runnable retries,
subset exclusion, historical ownership, two-pass recovery and intervening failing executions.
Executions lacking `run_id` remain independent, including rows sharing a timestamp;
a regression checks both current and legacy collection keys. No full suite or build was run.

Read-only replay used actual private history rows from `20260921T233422Z-ba41` (11 missing
`fake_cards` errors) and its `-rerun` (one wrapper AttributeError), actual signal claim events,
and fresh JUnit output from a valid temporary module. No private history/events were written.

| Replay stage | Historical wrapper | Historical run container | Owner |
| --- | --- | --- | --- |
| Recorded failures | open; 2 distinct executions | open; 2 failing runs | codex-hq |
| Two passing method subsets | open; 0 green | open; 0 green | codex-hq |
| First passing full module | open; 1 green | open; 1 green | codex-hq |
| Second passing full module | resolved; 2 green | resolved; 2 green | cleared by normal fold |

At initial implementation, the live signals remained open and owned; no takeover, dismissal
or manual closure was performed. Authorized live verification followed, as recorded below. Board MCP discovery returned no relay_board tools. This package
used the assigned parent scope, left HG26/AQ6X cards and threads untouched, and did not delegate.

## Authorized live verification

Two complete `tests.test_board_protocol` executions passed: **177 tests each**, in 15.524s and
15.297s. Both used `PYTHONPATH=backend:tests` (absolute equivalents), temporary XDG directories,
and `python3 -m relay_core.junit_runner --junit <temporary>/unittest.xml --root <repo> -q tests.test_board_protocol`.
Each report was parsed through `test_history.ingest_junit` with its actual commit/tree identity
and unique run ID, then appended with `test_history.append` to the normal private history store.
Each stored 177 test passes plus one full-module collection pass.

- `hg26-collection-20260922T165814Z-8da95bce`: both historical signals remained open, green streak 1, held by `codex-hq`.
- `hg26-collection-20260922T165830Z-aee9d126`: both resolved naturally, green streak 2; the fold cleared claims on resolution.

No signal event was written, no claim was changed manually, and neither card was edited.
Exact commands, output, commit/tree identities, scope rows and before/after signal snapshots:
[`collection-live-recovery.json`](collection-live-recovery.json).
