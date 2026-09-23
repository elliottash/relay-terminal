---
id: EFM7
type: work
status: needs-verification
labels: [bug, models]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mefm7
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [c1e6ea7154accc193a00c7f915b44cf08522245e], evidence: [docs/qa_evidence/2026-09-22-EFM7/results.txt, docs/qa_evidence/2026-09-22-EFM7/model-matrix.txt], related: [], github: null}
---
# New pane changes its displayed effort on first submission

## Issue
there is an issue when i open a pane, it says gpt 6 astra medium effort, but when i press enter, it changes to high
was the error specific to codex? check all models work as intended

## Done means
- Starting a Codex pane with medium effort reports medium in the configured event and runs the harness at medium.
- Model switches report the harness's actual effort, including levels such as xhigh and ultra.
- API-only panes retain their existing effort reporting.
- Every bundled guest model preserves each offered level through startup, set_effort, and model switching.
- API models with no effort control send no inherited effort parameter; supported API levels agree with the configured response and provider parameters.

## Plan
Goal: keep startup effort reporting consistent with the running harness.
Findings: guest_harness_provider.configured_fields reports guest_effort but leaves the generic effort field on the wrapper Agent's value.
Steps: reproduce through the worker; publish the harness effort in the shared event fields; update protocol documentation and run targeted tests.
Risks: legacy callers with no explicit guest effort must not be given a fabricated API level.
Verify: worker protocol regressions using FakeHarness, plus the guest provider and session protocol tests.

## Execution Summary
The generic effort field now reports the active guest harness's effort on configure and immediate model changes. This prevents the wrapper Agent's API effort mapping from changing medium to high or xhigh/ultra to max in the picker. The protocol documents the shared value. No C++ or layout changes; worker protocol behavior is tested with FakeHarness. Live GUI verification remains for a separate session.
Follow-up audit reproduced the old medium→high report on Claude Code as well as Codex. The permanent worker regression now covers 44 guest model/level combinations. It also found and fixed inherited effort fields on API models without a knob (Kimi variants and dynamic OpenRouter rows); backend capability lookup now follows the cached OpenRouter catalog. 489 current API model entries / 1507 effort cases passed the configuration audit. No live inference was performed; custom/local models without metadata remain outside exhaustive coverage.

## Tests
- `PYTHONPATH=backend:tests python3 -m unittest test_guest_harness_provider test_session_protocol` — 104 passed.
- `PYTHONPATH=backend:tests python3 -m unittest test_model_switch` — 22 passed.
- manual: docs/qa_evidence/2026-09-22-EFM7/results.txt
- `PYTHONPATH=backend:tests python3 -m unittest test_guest_harness_provider test_session_protocol test_presets test_roles test_guest_harness_claude test_guest_harness_codex test_model_switch` — 403 passed.
- `ctest --test-dir build -R '^(modelcatalog|modelrows)$' --output-on-failure` — 2 passed.
- `PYTHONPATH=backend:tests python3 docs/qa_evidence/2026-09-22-EFM7/audit_models.py` — 489 API model entries / 1507 effort cases passed.
- manual: docs/qa_evidence/2026-09-22-EFM7/model-matrix.txt
