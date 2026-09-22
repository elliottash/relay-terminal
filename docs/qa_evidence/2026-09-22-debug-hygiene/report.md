# Debug and hygiene workstream — 2026-09-22

Proposal for #HG26. This is an analysis and work sequence, not a claim that the proposed fixes have landed.

## Evidence and scope

The saved `worker-summary.json` covers retained worker records from 00:00:19 to 14:23:31 UTC on September 22 (three rotated files): **3,938 tool results, 337 marked unsuccessful, 140 completed turns, 10 cancelled turns and 9 error turns**. There are **97 protocol errors** and **21 HTTP retry events**. This is local, mixed development/QA/interactive traffic, not a production failure rate. Files were read while live, so this is a bounded observation rather than an atomic snapshot.

Other sources: today's retained `relay.log*`; the current empty `worker-faults.log`; `relay-board.py signals`; `issues/.private/tests/history.jsonl`; relevant card bodies and threads; source paths below. The Board index is stale in places: #YJG7's file is discussing even though the index says inbox, and #MSW7 is absent from that index. Treat card files as authoritative.

No full suites, paid provider probes, changes to existing cards, signal claims or dismissals were performed. The two open signals belong to `codex-hq`. Board MCP read/write tools were absent from discovery; local files and the supported signals CLI supplied the evidence.

## Findings that change the interpretation

| Observation | Interpretation and confidence |
|---|---|
| `agent_wait`: 85 of 105 results marked unsuccessful, in four sessions | The result classifier treats any `timed_out` as failure (`Agent._record_tool`, `agent.py`). `Subagents.wait` returns that field for a normal polling deadline, and the guest bridge caps waits at 10 seconds. Two affected Codex transcripts contain many `timed_out: true` wait responses. Confirmed classification problem; do not claim all 85 have the same cause without joining every call. |
| GUI: 45 ERROR `worker_exit` records | All 45 have `code=0 crashed=0`. `Pane.h` logs every worker exit at ERROR before testing closing/reconfigure state. These are not 45 crashes; whether any individual clean exit was unexpected requires lifecycle context. No crash was established by this scan. |
| 97 protocol errors | 30 are `tiers.flash`/`roles.flash` rejecting preset `nope`, matching explicit negative tests in `tests/test_roles.py`. Other examples include invalid queue modes, unknown skill names, and missing test credentials. Tests can inherit a live pane ID, so `pane=-` is not a reliable test filter. Some events are real user-facing failures, including the role-switch rejection correlated in #MSW7. |
| Nine error turns | Five are unavailable localhost model servers with ephemeral ports and no pane ID: consistent with test traffic, not independently proven for every turn. The other four are a URL-validation failure, a guest model-capacity error, and two GLM/fallback failures. Do not report nine user incidents. |
| Unsuccessful shell tools | In the earlier 14:20–14:22 drill-down, 199 of 211 failed `run_command` records lacked `exit_code`; 11 had code 1 and one code 2. Missing metadata prevents reliable separation of expected command outcomes, invocation errors and application defects. |
| Two open signals | Both derive from run `20260921T233422Z-ba41`: 11 repeated loader failures (`fake_cards` missing), then one automatic rerun of `_FailedTest.test_board_protocol` that raises AttributeError. Later runs at 23:34:53 and 23:34:55 pass 11 real tests each; a 599-test run at 01:39:51 also passes. The synthetic key never appears as passing, leaving both it and the run container open. This is an identity/collection recovery problem, not evidence of 12 currently broken board tests. |

The initial report's headline counts were useful for locating clusters but overstate actionable defects. It also skips 1,449 nonmatching/malformed lines across the scanned files; skipped lines are not time-filtered. Those are coverage diagnostics, not 1,449 errors in today's window.

## Recommended sequence

### 1. Make the measurements trustworthy (first small package)

Add a bounded outcome taxonomy to tool records: success, pending, refused, command_nonzero, timed_out, transport_error and internal_error. A polling deadline is pending; a subprocess deadline remains a timeout. Preserve the tool name, call/turn/session IDs, duration and structured error code without retaining command arguments or output. Keep refusal presentation consistent with the work already landed for #25XG rather than redoing its UI.

Isolate test-worker logging with a temporary XDG data directory, and add explicit origin (`interactive`, `test`, `qa`) plus run/build identity. A pane ID is not origin. Log expected shutdown/reconfigure at INFO and unexpected termination at ERROR, with a reason. Extend `scripts/relay-events.py` to show denominators and unknown classifications, failure reasons, p50/p95 latency and origin breakdowns. Preserve unknown historical rows instead of retroactively guessing their meaning.

**Acceptance:** A normal guest wait poll and intentional restart produce no defect count; a timed-out subprocess and unexpected worker termination remain visible; negative tests leave the user's logs unchanged; native and guest tool results produce equivalent categories. Reports disclose parse coverage and retention. Do not automatically create cards from raw `ok=False`.

### 2. Fix the known user-facing model/provider cluster (highest product priority)

Use existing **#MSW7** (discussing) and **#YJG7** (discussing). #MSW7 already has no-network reproductions and session evidence: a guest-role selection attempts to construct an HTTP provider from `harness://codex`, role state changes before transport creation succeeds, and fallback reporting repeats GLM's error instead of Muse's actual error. #YJG7 has earlier provider evidence of quota exhaustion; today's 429 bodies are absent, so today's exhaustion remains an inference.

Today's logs contain two GLM turns lasting 24.461 and 27.684 seconds, with six retry waits each before fallback failure. Across the window, 17 of 21 HTTP retries are GLM and four are Kimi. Address transport-aware, atomic model switching; truthful fallback diagnostics; typed quota-vs-transient rate-limit handling with reset-aware retry suppression. Preserve interruption/model switching during retry.

**Acceptance:** Inject failed native→guest and guest→native switches; the selected role/model agrees with the actual provider after success or rejection. Two distinct provider errors remain distinct. An exhausted quota does not receive a transient retry loop; genuine transient 429s still retry. Follow deterministic tests with one isolated GUI flow. Reuse existing cards and evidence; no duplicate model-switch initiative.

### 3. Repair signal collection and recovery semantics

Follow up **#AQ6X** (needs-verification) with the existing holder of the two signals. Treat import/collection failures as a collection key tied to the requested module/scope; never rerun unittest's `_FailedTest` wrapper as a test. Deduplicate repeated synthetic failures within a run. Make successful collection of that same scope resolve the collection/container state under the documented consecutive-pass rule, without letting unrelated green tests close it.

**Acceptance:** Replay the recorded sequence: repeated loader failure → valid module discovery → two passing executions. One collection incident should be tracked and then resolved. Unrelated passing subsets must not resolve it. Do not manually mark the existing signals fixed or silently take over `codex-hq`'s claims.

### 4. Clear evidence/status debt before adding more QA automation

- **#WEVT:** file remains inbox, but its execution summary says fixed by `e4bfa994`, and recorded history contains 48 passing remote-wire tests. Revalidate against the intended revision and reconcile status; don't reimplement the event classification.
- **#40SN:** configure recovery already has implementation evidence, but its card records missing current-revision evidence for `test_configure_recovery.py`. Finish that targeted validation and the bounded startup/recovery GUI scenario.
- **#SW1D:** Hygiene/Tests/Performance row already landed. Its outstanding verification is the live model-generated cleanup preview and successful Apply with a configured isolated provider. Don't rebuild the buttons.
- **#SJTR:** broader QA attack system is still discussing/waiting on owner. Feed this workstream's classified incidents into that design later; no need to make a new dashboard a prerequisite for reliable local counts.

**Acceptance:** Each touched card names an implementation revision, reproducible check and current evidence; regenerate the index from the files, preserving other sessions' work. A status change follows verification, never a title match.

### 5. Establish a lightweight recurring review

Start with manual daily snapshots and a weekly review, not automatic scheduling in this change. Capture aggregate JSON outside git with a chosen retention window; preserve only selected sanitized incident evidence in git. Track unexpected tool errors per 100 completed calls, affected sessions/turns, repeated retry time, pending wait polls separately, and unresolved signal age with owner. Join incidents to existing cards by explicit IDs and evidence, not merely similar error prose.

Review the top three confirmed regressions and the oldest unresolved owned signal first. Promote only a reproduced defect or a recurring, attributable failure cluster. Expand into #SJTR once the measurements distinguish known negative tests and normal operations from faults.

## Verification performed

- `python3 scripts/relay-events.py --since 2026-09-22 --json` — saved aggregate snapshot.
- `python3 scripts/relay-board.py signals` — exactly two open signals, both held by codex-hq.
- Read-only grouping of protocol error signatures, worker exits, failed-turn causes and per-session wait counts.
- `python3 scripts/relay-board.py check` — no findings reference HG26 or its files; the shared board has 12 existing errors and 754 warnings. The MCP `tests_check` tool is not exposed; this analysis has manual evidence rather than implementation test results.
- Compared private run-history rows for the loader failure and three later successful runs.
- Inspected classifier, wait/bridge, process-exit and signal folding code; compared relevant card bodies and threads with their existing evidence.

This proposal does not assert causality for unclassified historical failures and does not substitute for reproducing the proposed fixes.
