---
id: JX8Z
type: work
status: needs-verification
labels: [feature, models, routing, logging]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 323e184d-caae-46e5-9de8-9161a0c1e62e
rank: zzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [probe], human: none, sign_off: none, effort: high, stakes: rework, blast: capability}
source: Relay pane, 2026-09-25
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-JX8Z/evidence.md], related: [495G], github: null}
---
# Record timestamped subscription usage states and restore fresh routing scores

## Issue
yes, plan that on a card and deliver it

## Done means
Relay appends timestamped, account-specific raw usage snapshots (5h/weekly windows, reset times, status, banked reset count/expiry, source) to a durable private JSONL file whenever fresh usage arrives. Routing can use fresh Claude and Codex data at choices after startup, and a live probe shows scored candidates rather than median placeholders. Records can be joined to draw records without exposing credentials or prompts.

## Plan
**Goal:** Keep an append-only history of the raw quota state used for routing, and ensure subscription guest states reach the picker before choices.

**Findings:** `backend/relay_core/guest_harness_provider.py` keeps guest limits in process memory; `backend/relay_core/provider_limits.py` polls direct providers; `src/PaneSession.cpp` receives `usage_limits`; `src/ModelCatalog.cpp` scores only fresh catalog limits. The existing `routing-draws.jsonl` contains the derived choice but no raw history.

**Steps:**
1. Trace guest quota refresh and preset propagation; identify why Claude/Codex rows lack fresh scores.
2. Add a shared, private, append-only `usage-states.jsonl` writer with timestamp, account key, source, raw windows, status and reset fields; write at the authoritative event boundary, deduplicating concurrent pane reports if needed.
3. Refresh/propagate guest limits so a new-pane or mode draw sees current data; preserve the 30-minute freshness rule and no-spend reset rule.
4. Add targeted tests for serialization, privacy, freshness and routing; document join keys and run a live probe.

**Risks:** Multiple workers share one file and guest quota may be unavailable before authentication; write atomically and record unavailable states without inventing numbers. Other sessions are editing large pane files, so land only owned hunks.

**Verify:** Targeted Python/Qt tests and a probe of new JSONL rows and scored Claude/Codex draw candidates in a fresh Relay process.

## Tests
`PYTHONPATH="$PWD/backend" python3 -m unittest tests.test_guest_usage_poll tests.test_logs`
`QT_QPA_PLATFORM=offscreen build/relay-modelcatalog-tests`
`scripts/relay-build`
manual: `docs/qa_evidence/2026-09-25-JX8Z/evidence.md`

## Execution Summary
Added `usage-states.jsonl` logging at the worker's `usage_limits` boundary, with whitelisted raw windows, timestamps, preset/account identifiers, source, status and optional banked reset fields. Added read-only Claude/Codex polling every 15 minutes for signed-in Relay accounts; incoming snapshots refresh preset rows. A new-pane draw can load a recent recorded state before its worker has reported, while a newer `presets` update no longer loses to an older pane cache. Evidence: `docs/qa_evidence/2026-09-25-JX8Z/evidence.md`.
