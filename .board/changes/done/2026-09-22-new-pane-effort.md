---
id: EFM7
type: work
status: done
labels: [bug, models]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
verified_by: anthropic/claude-opus-5-5 via claude-code
rank: mefm7
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [c1e6ea7154accc193a00c7f915b44cf08522245e, f4bde4dd39b05bcf008a7693637937c72e28d2f9], evidence: [docs/qa_evidence/2026-09-22-EFM7/results.txt, docs/qa_evidence/2026-09-22-EFM7/model-matrix.txt, docs/qa_evidence/2026-09-22-verify-EFM7/results.txt], related: [], github: null}
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

## QA checklist
Revision checked: clean export build `/tmp/efm7-src-TvGu/build/relay` (sha256 in `docs/qa_evidence/2026-09-22-verify-EFM7/provenance.txt`), live GUI under Xvfb with real Codex and Claude Code CLIs. Evidence: `docs/qa_evidence/2026-09-22-verify-EFM7/results.txt`.

- Codex pane at medium reports medium and runs at medium: **passed** (configured effort=medium; picker still medium after first reply, `codex-response.png`).
- Model switches report the harness's actual effort incl. xhigh/ultra: **passed** (Codex xhigh/ultra, switch to gpt-6-sol keeps ultra; Claude xhigh, switch to sonnet keeps xhigh, `codex-switch.png`, `claude-switch.png`).
- API-only panes keep their reporting: **passed** (openai gpt-6-astra medium stays medium after submit, `api-openai-after.png`; relay-main high→medium is correct, it offers low|medium only).
- Every bundled guest model preserves each level: **passed** for the live cases (Claude medium → `--effort medium`, xhigh → relaunch `--effort xhigh`); remaining combinations rest on the 44-case worker regression.
- No-knob API models send no effort; supported levels match the provider param: **passed** (GUI payload replayed: openai medium/xhigh/low sent as given despite preset `high`; kimi-for-coding sends `{}`).
- Tests: 404 targeted backend tests OK (`tests.txt`).
- Unresolved: no live API inference (no real OpenAI/Kimi key here).

Reviewed 2026-09-22 by Codex (Codex drive) and Claude Opus 5.5 (Claude, API drives, verdict).

## Verdict
Pass. A new pane keeps its displayed effort on the first submission for Codex, Claude Code and API models; effort changes and model switches keep the harness level, and API requests carry the chosen level (or none for models without a knob).
