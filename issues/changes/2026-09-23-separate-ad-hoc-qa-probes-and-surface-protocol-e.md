---
id: KHY0
type: work
status: needs-verification
labels: [bug, diagnostics, qa]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 7da1eefd-bd8b-46d7-8063-a77023e6a273
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: Codex follow-up log review, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-khy0/report.md], related: [HG26, 40SN, 0C0V], github: null}
---
# Separate ad hoc QA probes and surface protocol exception clusters

## Issue
analzye the errors and plan any useful fixes, and put them on cards

## Planning notes
The post-delivery window (2026-09-22 16:54:51 to 2026-09-23 21:46 UTC) contains 267 `protocol_error` events but zero classified transport/internal *tool* outcomes; those metrics measure different boundaries. In the 2026-09-23 12:00–21:46 UTC subset, 129 protocol errors carry `origin=interactive`: 127 are ValueError/KeybindingError guard paths and two are `configure` AttributeErrors. Many guard clusters occur in short-lived test-style workers, yet ad hoc direct worker invocations default to interactive origin. The reporter groups protocol errors only as an undifferentiated event count, requiring a raw-log drill-down to find unexpected exception types. Do not treat every ValueError as a product defect or export its free-text message.

## Done means
A supported disposable QA probe records `origin=qa` with a bounded run ID and writes no diagnostic records into the user's normal Relay log. The event report separates protocol exceptions by safe `kind` and exception class, so unexpected configure exceptions are visible alongside tool outcomes without copying messages, prompts, arguments, paths, or identifiers into aggregate snapshots. Normal validation refusals remain visible but are not counted as internal tool failures; historical records remain explicitly unknown.

## Plan
**Goal:** make future post-delivery reviews distinguish expected protocol rejection from unexpected worker exceptions and keep ad hoc QA probes out of interactive logs.

**Findings:** `scripts/relay-events.py` already parses worker records but aggregates protocol errors only by event/level. `backend/relay_core/logs.py` defaults origin to `interactive`; supported `scripts/test.sh` and `relay_core.junit_runner` isolate test logging, while direct temporary worker/GUI probes can inherit normal storage and label themselves interactive.

**Steps:**
1. Add a small documented QA launcher or helper for disposable worker/GUI probes that sets `RELAY_LOG_ORIGIN=qa`, a bounded run ID, and isolated XDG data/config paths before startup. Migrate the project's ad hoc QA recipes that currently launch workers directly.
2. Extend `scripts/relay-events.py` with a bounded protocol-error table grouped by `kind`, exception class, origin and count. Sanitize each token; preserve `unknown` for missing historical fields. Keep it distinct from classified tool outcome rates.
3. Add tests using a mixed fixture (expected ValueError, unexpected AttributeError, malformed/legacy record) and a subprocess probe proving no production-log write. Update `docs/DEBUG-HYGIENE.md` with interpretation and the QA invocation.

**Risks:** Exception `msg` may contain user content; never write it into snapshots. Some direct probes intentionally target a real profile; do not silently change their storage semantics. Do not infer bugs from exception class alone.

**Verify:** Reporter unit tests, one isolated probe, a sample report showing the two configure AttributeErrors separately from guard errors, and review of output for sensitive fields.

## Execution Summary
Added `scripts/relay-qa-run` for disposable QA worker/GUI probes with private XDG directories, QA origin/run identity, keyring/local-model/memory-import isolation and optional retained profile. `scripts/relay-events.py` now reports protocol-error counts by allowlisted request kind, exception class and origin, separately from tool outcomes. Unknown/legacy fields stay unknown; free-text messages and identifiers are excluded. The live post-delivery report surfaces the two known `configure`/`AttributeError` events. Documentation and evidence: `docs/DEBUG-HYGIENE.md`, `docs/qa_evidence/2026-09-23-khy0/report.md`.

## Tests
- `tests/test_event_report.py`
- manual: docs/qa_evidence/2026-09-23-khy0/report.md
