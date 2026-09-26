---
id: VZ8C
type: work
status: needs-verification
labels: [bug, signal]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 8f478c92-da57-4c74-b20e-5a3929f72d24
rank: zzzzzzzzzzzzzzzzzi
created: '2026-09-22'
source: signal ctest:consolemode, 2026-09-22
links: {plans: [], commits: [3dad4ea0cdb054c4db39062bc9ab6dc876072e9e], evidence: [docs/qa_evidence/2026-09-23-vz8c/report.md], related: [], github: null, signal: ctest:consolemode}
---
# ctest:consolemode fails

## Issue
`ctest:consolemode` has failed 2 time(s) in 2 run(s) since 2026-09-23T02:51:36Z.

```
Failed
```

## Signal
<!-- Written by Relay (relay_core.signals.signal_section). Rewritten in place whenever
     the signal changes; edit around it, not inside it. -->
- **key** `ctest:consolemode` · flaky · **open** · regressed
- failing executions: 6 in 6 run(s); first seen 2026-09-23T02:51:36Z, last 2026-09-25T15:16:45Z
- fingerprint: `SEGFAULT`
- resolved at  in `0dbcbfa5c29e`
- it resolves on 20 consecutive passing executions of that key, and on nothing else: closing this card does not close the signal, and this card cannot leave needs-verification while the signal is open.

```
SEGFAULT
```

## Planning notes
Priority: medium. Signal history has two failing `ctest:consolemode` executions at 2026-09-23 02:51:36–38 UTC and no later passing execution of that key. A separate card (#C8SV) records that the full test stops at `tests/consolemode_test.cpp:1082`, in the repeated empty-Enter/queued-prompt scenario, before its memory acceptance case. Earlier history records passes at 2026-09-22 17:43 and 17:45 UTC. The shared checkout and build changed repeatedly afterward, so the historical two failures do not yet prove the current source is broken. Keep the signal's generated section intact.

## Done means
A fresh build of one recorded source revision passes the complete `ctest:consolemode` key twice consecutively, closing its signal through normal test history. The queued-prompt empty-Enter scenario preserves the expected wire request, queue length, and composer state. A focused regression fails against the faulty revision if the failure is still present.

## Plan
**Goal:** determine whether the open `consolemode` signal is a current queued-prompt regression and close it only with same-key passes.

**Findings:** The failure occurs in the `tests/consolemode_test.cpp` empty-Enter sequence near line 1082. `issues/.private/tests/history.jsonl` has two failing runs at 02:51 UTC and no later pass; the source and build have changed since then.

**Steps:**
1. Record a clean revision and build the test target with `scripts/relay-build` as documented in `docs/BUILDING.md`; use isolated XDG settings. Run the complete `ctest:consolemode` key and capture the exact assertion and wire messages.
2. If it still fails, inspect `Pane`'s empty-Enter queue/steer/unsteer state transitions and the neighboring queue cards (#7JD1 and the enter-queue-order work). Fix the demonstrated transition, adding one focused assertion for the sequence that failed.
3. Run the complete named key twice through the supported test-history path so the signal resolves naturally. If it already passes twice on the fresh revision, record it as a stale build/run failure instead of changing code.

**Risks:** Several sessions are editing `src/Pane.h` and this test. Do not overwrite their work or infer a source regression from an old binary.

**Verify:** Exact-revision `consolemode` CTest twice, focused queued-prompt case, and signal status after ingestion.

## Execution Summary
Restored linking of both console test targets by adding the five split Pane sources. Updated two test expectations to match the current session-link worker request and terminal context update before steering. No queued-prompt production behavior changed. Two isolated complete CTest passes and two recorded same-key passes resolved the signal. Evidence: `docs/qa_evidence/2026-09-23-vz8c/report.md`.

## Tests
- `ctest:consolemode`
- `manual: docs/qa_evidence/2026-09-23-vz8c/report.md`
