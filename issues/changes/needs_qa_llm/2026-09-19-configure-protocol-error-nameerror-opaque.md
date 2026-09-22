---
id: 40SN
type: work
status: needs-qa-llm
labels: [bug]
component: [worker, gui]
milestone: desktop-alpha
workstream: agent
assignee: codex
rank: zzzz111
created: '2026-09-19'
acceptance: a pane whose configure dies on an unexpected worker exception shows the exception's own message (e.g. "name 'os' is not defined"), recovers without being closed once the backend file is repaired, and a test covers the reporting and the recovery
source: 'conversation, 2026-09-19: "im getting this bug, protocol error (nameerror) when im trying to work in a new pane"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-hg26-verification/report.md, docs/qa_evidence/2026-09-21-pane-startup-recovery/README.md], related: [], github: null}
---
# A failed configure shows only "Protocol error (NameError)." and the pane never recovers

## Issue

"im getting this bug, protocol error (nameerror) when im trying to work in a new pane"

## Planning notes

Incident: 2026-09-19, 04:43–04:45 UTC

New panes (`99394410`, `612b49a7`) failed their first `configure` with `Protocol error (NameError).`
eleven times, ~every 5–20 s, until the panes were given up on. `worker.log` has what the pane does not:

```
protocol_error kind=configure error=NameError msg="name 'os' is not defined"
```

Cause of the NameError itself: the backend tree was mid-edit by a parallel agent session (an
`edit_file` on pane `5b9e3f00` completed 19 ms before the first failure; `backend/relay_core/agent.py`
was saved 04:39:33, `provider.py` again at 04:47:34, after which the failures stopped). A pane worker
that starts while a module is transiently broken imports that module and then fails **every** retry of
`configure` in that process — Python never re-reads the file, so the pane stays broken even after the
tree is fixed, until the pane is closed. The same shape occurred 2026-09-18 19:04 UTC
(`name 'sessions_usage' is not defined`, 6 hits).

The current tree is clean: `configure` run end-to-end against the real worker succeeds (kimi and
openrouter presets), and every backend module compiles. Nothing is left to fix in the code that raised.

The two gaps identified at planning:

1. **The message is thrown away.** `worker.py`'s handler emits the exception text only for
   `ValueError`/`OSError`/`KeystoreError` — anything else is redacted to
   `f"Protocol error ({type(exc).__name__})."` so prompt text can never leak. But a `NameError`,
   `AttributeError` or `TypeError` message ("name 'os' is not defined") names a code defect, carries
   no user text, and is exactly what the person at the pane needs in order to report it. It is already
   in `worker.log`; the pane just refuses to show it.
2. **No process-level recovery.** The GUI retries `configure` in the same worker forever. For an
   unexpected exception the worker process is the broken unit (cached import); the only fix is a
   restart, which the GUI already knows how to do when a worker dies.

## Plan
**Goal:** Show why initial configuration failed and recover in a fresh worker without closing the pane.
**Findings:** `backend/worker.py` redacts unexpected exceptions; `src/Pane.h` retries in the same process.
**Steps:**
1. Report structured configure failures with the exception kind and diagnostic text.
2. Restart the pane worker once automatically and replay the failed configuration; provide an explicit retry after repeated failure.
3. Preserve deferred Plan/Build selection before the first prompt (#MDP1).
4. Test worker diagnostics, fresh-process recovery, repeated failures, and pre-start planning; document the wire fields.
**Risks:** Do not restart a shared worker or a worker with an active turn. Bound automatic retries and preserve queued prompts and provider selection.
**Verify:** Targeted Python tests, the application build, and isolated Xvfb checks with a deterministic fake worker.

## Tasks

- [x] `worker.py`: include `str(exc)` in the `error` event for unexpected exception types whose <!-- t:t9 -->
      messages are code, not prompts (`NameError`, `AttributeError`, `TypeError`, `ImportError`),
      keeping existing `ValueError`/`OSError`/`KeystoreError` reporting; keep `worker.log` as the full
      record
- [x] `src/Pane.h`: on a configure error of that unexpected kind, surface the message in the status <!-- t:bh -->
      line (attributed, like the #308N suggestion line) instead of the bare "Protocol error (…)"
- [x] `src/Pane.h`: after a configure failure of that kind, restart the pane's worker (or restart <!-- t:ph -->
      after N consecutive configure failures) so a repaired tree is picked up without closing the pane
- [x] `docs/AGENT-SESSIONS-PROTOCOL.md`: document the error-event shape for unexpected exceptions <!-- t:7t -->
- [x] Test: a worker whose configure path raises `NameError` reports the message (not the redacted <!-- t:0h -->
      line) and the pane retries configure in a fresh worker process, which then succeeds

## Decisions

- Owner: "claim all of these and implement them".

- 2026-09-19, agent: filed from a live incident; the redaction is kept for the three prompt-carrying
  types and widened only for exception types whose message is a code symbol — not a blanket unmask.

## Execution Summary
The worker reports structured `configure_failed` diagnostics for NameError, AttributeError, TypeError and ImportError. A pane retries the identical configuration once in a fresh process, then offers Retry agent after repeated failure. Queued prompts and Plan/Build selection survive; shared workers and active turns are not restarted. The mode chosen before a deferred guest starts is applied before its first ask (#MDP1).

## Tests
`tests/test_configure_recovery.py`
`tests/test_session_protocol.py`
manual: docs/qa_evidence/2026-09-21-pane-startup-recovery/README.md

### Check 2026-09-22 12:58
- passed · unittest:tests.test_configure_recovery — tests/test_configure_recovery.py passed for this revision on spark-dcc9, 2026-09-22T16:58:18Z
- passed · unittest:tests.test_session_protocol — tests/test_session_protocol.py passed for this revision on spark-dcc9, 2026-09-22T02:15:37Z
- not-applicable · manual:docs/qa_evidence/2026-09-21-pane-startup-recovery/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-21-pane-startup-recovery/README.md
- notice · unittest:tests.test_session_protocol — tests/test_session_protocol.py: 7 of 35 are slow (test_the_row_moves_while_nobody_is_typing, test_two_claudes_in_one_directory_tail_their_own_sessions, test_indexing_the_guests_off_means_no_tail…)
history: thread
## QA checklist
- [x] Open a new Codex pane; select Plan before typing; the first prompt runs in Plan.
- [x] Inject an initial configure defect; verify its diagnostic is visible and a fresh process recovers without reopening the pane.
- [x] Repeated failure stops after one automatic retry; after repair, Retry agent (or Ctrl+Shift+R) submits the queued prompt once.

## Verdict
PASS independent verification, 2026-09-22: four recovery tests and four fresh Xvfb startup scenarios pass. Startup fault injection uses a scripted worker; real Kimi worker startup also succeeds in the separate cleanup scenario. Current shared-build evidence, not an exact-commit release gate. Forwarded to QA. Evidence: docs/qa_evidence/2026-09-22-hg26-verification/report.md
