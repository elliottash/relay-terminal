# HG26 independent reporter review — 2026-09-22

Reviewed reporter commits `2f257ae6` and `6e3d6d00`, plus formatter interoperability after logging commit `5304f3a2` landed (tip `3edb502a`). Production source and HG26 board files were not changed.

## Finding requiring correction

**Malformed snapshot timestamps abort retention and review.** In `scripts/relay-events.py:188` and `:202`, `timestamp(data['generated_at'])` raises an uncaught `AttributeError` when a matching JSON file contains `generated_at: null` or a number. A single such file prevents weekly review; snapshot creation writes the new file but then exits unsuccessfully during pruning. This also occurs for unrelated-kind JSON using a matching filename, because timestamp parsing precedes the kind check. Invalid JSON already skips correctly. Validate timestamp type (raising a handled ValueError/TypeError), and check the snapshot kind before processing its timestamp.

Reproduce both failures:

```bash
python3 docs/qa_evidence/2026-09-22-hg26-report-review/check.py
```

Observed: 7 tests, 5 passing, 2 errors. Retention raises `AttributeError: 'int' object has no attribute 'replace'`; review raises `AttributeError: 'NoneType' object has no attribute 'replace'`. These are two manifestations of one defect. No source fix applied: this bounded review required messaging the parent before changes, and this guest harness exposes no relay_board messaging tools. This report hands the failure and executable reproducer back to the parent.

## Passing checks

- Existing reporter tests: `python3 -m unittest discover -s tests -p test_event_report.py -v` — 5 passed.
- Historical `ok=False` remains unknown; a pane name never invents a test origin. Unknown/QA/interactive filters work. Pending/refused/unknown-only records yield a zero completed denominator and null rate. Negative durations are excluded.
- Existing classification test establishes five completed categories and 40 unexpected errors per 100 completed; new independent formatter test establishes 100 per 100 for one internal failure.
- Real `logs.configure` + `logs.event` writes are parsed with appended origin/run/build metadata. QA filtering, retry wait, session/turn cardinality and outcome rate are correct. Aggregate export excludes synthetic arguments, output, error text and raw session/turn identities. Retested this case after `5304f3a2` landed: 1 passed.
- Real temporary history/action stores fold to the explicit promoted card and owner. Error messages/excerpts are absent from export; both stores remain byte-identical after reading.
- Retention preserves exact-boundary snapshots, foreign-kind JSON, invalid JSON and symlinks; deletes the valid snapshot older than the cutoff; creates the new snapshot with mode 0600.
- Overlapping windows retain latest-per-day/filter records without summing. Separate QA and all-origin windows remain separate; future snapshots are excluded.

The tests use temporary synthetic data, create no live signals and read no private runtime logs. No full suites, builds, further delegation or board writes were performed. Relay tools were searched in the harness tool catalog and were unavailable; no connection or interim parent-message delivery is claimed.
