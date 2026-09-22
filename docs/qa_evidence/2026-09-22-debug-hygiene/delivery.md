# HG26 delivery record

Delivery authorized by the owner: “deliver all the work with subagents”. Relay delegated logging (a1), model/provider reliability (a2), collection recovery (a3), existing-card verification (a4), and independent report review (a5). Parent owns reporting and integration. This record is updated as verification completes.

## Report and review workflow

`2f257ae6` implements outcome/origin reporting, explicit unknown historical classifications, classified completion denominators, p50/p95, safe reason codes, affected session/turn counts, requested retry waits and read-only signal/card links. Private snapshots are created 0600; only recognized snapshots are pruned after 30 days. Weekly review keeps the latest window per day/filter and never sums overlapping logs. `6e3d6d00` indexes the workflow documentation.

Five focused report tests passed; a live daily snapshot and weekly review ran successfully. Routine snapshots stay in ~/.local/share/relay/diagnostics, outside git. No schedule installed, as the approved workstream calls for manual snapshots/review.

## Model/provider package

Atomic switching and distinct fallback errors already landed in `552982fa` with exact-tree build evidence `df9b35bf`; a2 independently reran the 14-stage isolated GUI drive. Quota suppression follow-up `24a6b202` scopes cached refusals to endpoint/model/key, expires within 60 seconds, bounds against the earliest possible provider reset, and honors Stop/model-change preemption. Unknown timezone is not converted to an invented timestamp. 220 targeted tests and the application build passed.

Parent independently ran five focused quota/failed-switch regressions: `PYTHONPATH=backend:tests RELAY_KEYRING=off python3 -m unittest test_provider.QuotaSuppressionTests test_guest_harness_provider.WorkerProtocolTests.test_failed_native_install_preserves_guest_and_high_role test_guest_harness_provider.WorkerProtocolTests.test_failed_guest_role_pick_keeps_api_model_and_main_role -q` — passed.

See ../2026-09-22-quota-suppression/report.md. MSW7/YJG7 remain in their justified verification lanes.

## Collection recovery

`9bef2478` introduces runnable collection scope, synthetic deduplication and guarded same-scope recovery; `4ac6499a` preserves separate observations lacking run IDs. a3 ran 268 targeted tests, and subprocess regressions cover module/subset distinctions, interrupted green streaks and legacy ownership.

`b6d1999c` records two real complete tests.test_board_protocol runs (177 tests each), with JUnit outputs ingested into normal private history. First pass preserved both signals and codex-hq ownership; second pass naturally resolved them. Parent independently confirmed `python3 scripts/relay-board.py signals` reports no open signals. No claims, dismissals or manual closures were used. See collection-recovery.md and collection-live-recovery.json.

## Existing card verification

`c26b0ec5` and `96e2e21d` record independent package-4 verification. WEVT's 48 remote-wire tests pass and its stale inbox status is reconciled to done. 40SN has four fresh recovery tests plus four isolated GUI startup/retry scenarios; it advances to needs-qa-llm. SW1D now has real Kimi-generated preview and explicit successful GUI Apply on a disposable two-card board; no production-board cleanup. It also advances to needs-qa-llm. Three relevant widget CTests pass.

See ../2026-09-22-hg26-verification/report.md for transcripts, screenshots, bounded live-provider use and shared-build provenance. These are independent observed flows, not an exact-commit release matrix.

## Independent report review

Reviewer a5 found malformed snapshot timestamps could crash review/pruning (`59086983`). Parent fixed timestamp/container validation in `e280f0ec`, added a regression across malformed types and reran all six report tests. Reviewer independently reran seven end-to-end checks, all passing, and appended the resolution in `53f59865`. Checks cover real formatter parsing, absence of raw result text/IDs, signal-store read-only behavior, retention boundaries/symlinks and non-additive overlapping windows. See ../2026-09-22-hg26-report-review/report.md.

## Logging and final integration

`5304f3a2` lands bounded native/guest classification and origin/run/build metadata. Intentional shutdown/reconfigure is INFO; an unexpected clean exit stays ERROR. The exact proposed C++ tree built successfully; a fresh isolated GUI lifecycle drive demonstrated all three cases. 123 targeted tests passed.

Review removed import-time storage mutation in `a89d2199`: only explicit test runners and child fixtures isolate environment, and imports leave storage untouched. Typed native/guest exception boundaries supply trustworthy codes; ambiguous errors remain unknown. `665814e7` independently exercises direct unittest role tests, proves live logs unchanged, and honors explicit XDG fixtures (85 tests passed). Supported runner and environment fields are documented in docs/DEBUG-HYGIENE.md.

Parent final checks:
- `PYTHONPATH=backend:tests python3 -m relay_core.junit_runner -s tests test_event_report test_tool_outcomes -q` — 15 passed.
- Fresh temporary recorder-to-report flow: three QA tool records classified as pending, command_nonzero and internal_error; zero malformed records; affected session count one. No production log writes.
- Independent reviewer checks: all seven pass after malformed snapshot correction.
- Signals CLI: no open signals, following recorded full-module recovery runs.
- Board format check: 12 pre-existing errors and 754 warnings; no HG26 findings. This work does not claim to repair unrelated board records.

All five proposed packages are delivered. The manual review workflow is operational; no automatic schedule or broader #SJTR QA attack system was installed. New runtime logging applies when workers load the updated backend. Large work remains in its verification/QA lanes under board policy; these lanes do not indicate unfinished implementation.
