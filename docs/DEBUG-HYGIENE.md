# Debug and hygiene review

Relay diagnostics stay on this machine. `scripts/relay-events.py` summarizes retained worker logs without exporting prompts, tool arguments/output or free-text errors. Logs rotate, and live reads are not atomic: use the reported first/last timestamp and coverage counts rather than assuming a complete day.

## Daily snapshot

Run from the repository (replace the date with the UTC day under review):

```bash
python3 scripts/relay-events.py --since 2026-09-22 --board issues --snapshot --json
```

Snapshots default to `$XDG_DATA_HOME/relay/diagnostics`, or `~/.local/share/relay/diagnostics`. Each file is created with mode 0600. The directory is created with mode 0700. Each invocation prunes only this tool's valid snapshots older than 30 days; `--retention-days` changes that window. `--snapshot /path/to/directory` selects another location. No scheduled task is installed. Keep routine snapshots out of git; commit only selected sanitized evidence for an incident.

Use `--origin interactive` for interactive sessions, `--origin test` or `--origin qa` for those runs. Historical records without an explicit origin stay `unknown`, even if they have a pane ID. Omitting the filter shows all origins and their counts. The report also preserves unknown historical outcomes rather than inferring a cause from `ok=False`.

`--board issues` adds open signal keys, age, owner and explicit promoted-card links. It only reads the private history and action stores: it neither claims signals nor changes their state. It does not guess a card from similar prose. Omit it when inspecting logs outside a project.

## Read the counts

- `calls` counts recorded tool completions/poll responses, not unique tasks or user incidents.
- `failed` / `failure_pct` preserve the old `ok=False` metric for historical comparison. They are not defect counts.
- `outcomes` distinguishes success, pending polls, refusals, command nonzero exits, timeouts, transport errors, internal errors and unknowns when instrumentation supplies those categories.
- `completed_classified` excludes pending, refused and unknown records. `unexpected_per_100_completed` counts transport/internal errors over that denominator; no denominator produces `null`, not zero. A command nonzero exit or timeout remains visible separately and is not automatically treated as a Relay bug.
- p50/p95 use the nearest-rank method over records with nonnegative durations. Missing durations are excluded; inspect `duration_samples`.
- `failure_reasons` contains only bounded machine codes, never exception text. A missing or unsafe code is `unknown`.
- `protocol_errors` groups worker protocol exceptions by known request kind, exception class and origin. It is separate from tool outcome rates: a rejected invalid request can be a normal guard, while an `AttributeError` deserves a traceback and reproduction. Unknown or untrusted kind/class values remain `unknown`; messages and IDs are never copied into the aggregate.
- Affected sessions/turns count distinct IDs associated with classified command/timeout/transport/internal failures. They exclude legacy unknown results and records without IDs.
- Retry wait is the sum of requested backoff seconds in retry events, not measured wall time actually spent sleeping. Cancellation can shorten a wait.
- Dated malformed lines are counted within the selected time window before origin filtering; undated lines are counted over all scanned files because they cannot be assigned to a window. Neither count is a failure count.

## Instrumented runs

New worker records append `origin`, `run_id` and `build_id` after the event fields. `RELAY_LOG_ORIGIN` accepts `interactive`, `test` or `qa`; invalid values become unknown. `RELAY_LOG_RUN_ID` supplies a run label, and `RELAY_BUILD_ID` supplies a package/QA build label. Identity values are bounded tokens, not arbitrary paths or messages. Without overrides the worker uses interactive origin, a process-run ID and a cached backend-source fingerprint. GUI worker exits record their running GUI build identity and lifecycle reason.

For a disposable worker or GUI probe, use the QA launcher:

```bash
scripts/relay-qa-run ./build/relay --workspace /path/to/disposable/project
```

It creates private XDG data/config/cache directories, sets a bounded QA run ID, disables the user's keyring and memory import, and removes the profile on exit. Use `scripts/relay-qa-run --keep …` to retain the profile and its logs for an incident; the launcher prints its path when the command ends. Use a disposable workspace too: the launcher isolates Relay's profile, not files passed as command arguments. Existing QA drivers that already isolate their profiles can set `RELAY_LOG_ORIGIN=qa` and `RELAY_LOG_RUN_ID` themselves.

Use the supported targeted test runner to isolate logs and label test origin:

```bash
scripts/test.sh --junit /tmp/relay-configure.xml tests.test_configure_recovery
```

`relay_core.junit_runner` also scopes its environment during execution. Importing production logging modules does not redirect storage. Hand-written test drivers must provide their own isolated child environment; do not infer that an arbitrary `python -m unittest` invocation isolates every fixture.

## Weekly review

```bash
python3 scripts/relay-events.py --review --days 7
```

This selects the latest snapshot for each UTC day and origin filter. It displays each window separately: repeated/cumulative windows are never summed into a misleading weekly total. Missing snapshots mean missing observations, not zero failures. For consistent comparisons, take snapshots with the same filter and explicit daily start time.

1. Compare classified transport/internal errors and affected sessions, with their denominators and unknown coverage. Investigate the top three recurring clusters before adding automation.
2. Check provider retry time and confirmed user-visible failures separately from polling and expected command results.
3. Review the oldest open signal and its owner. Read that signal's scope and run history; coordinate with the holder. Only recorded same-scope checks establish recovery.
4. Search existing cards and their threads before filing a defect. Link a reproduced failure to its session/turn, build, run and verification command. Promote evidence, not raw `ok=False` counts.
5. Revalidate implemented cards whose status/evidence is stale. The card file is authoritative; the Board index is generated. Never close a card because its title resembles a passing test.

The initial prioritization and baseline are in `docs/qa_evidence/2026-09-22-debug-hygiene/report.md` (#HG26). Model/provider defects use #MSW7 and #YJG7; collection-signal semantics relate to #AQ6X. The broader QA attack-system design remains on #SJTR.
