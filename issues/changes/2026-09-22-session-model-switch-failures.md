---
id: MSW7
type: work
status: needs-verification
labels: [bug, models, guest]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: zmsw7
created: '2026-09-22'
source: Codex session analysis in a Relay pane, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-model-session-audit/, docs/qa_evidence/2026-09-22-model-switch-fixes/], related: [4BPE, YJG7, GH5T], github: null}
---
# Role model selection can fail while leaving the selected mode changed

## Issue
analyze the weird behavior in model selection / changing in this session, i think its indicating some bugs

ef7ab5c3ef21445ab80dea50fedfbb29

## Discussion points
Analysis completed; fixes remain unimplemented. See the evidence report for the timestamped
timeline and the distinction between observed events and inferred click targets.

- A guest High-role pick resolves to harness://codex but Agent.set_model builds an HTTP
  ChatProvider and rejects the URL. Local no-network reproduction matches the session's
  13:09:30 UTC set_agent_role failure; the exact selected role/model was not logged.
- GUI and worker role state change before transport construction succeeds; a generic error
  does not restore the GUI role. The next turn in this session still used GLM.
- Main-page picks first acknowledge the previous Main model, then the requested model.
  Both explicit GLM and Kimi selections show this; Kimi ultimately did take over.
- Failover diagnostics suppress Muse's actual error and repeat GLM's error instead.
- Six GLM 429 retries per turn recur; quota handling is already tracked by #YJG7.

## Tests
- manual: docs/qa_evidence/2026-09-22-model-switch-fixes/tests.txt
- manual: docs/qa_evidence/2026-09-22-model-switch-fixes/build.txt
- manual: docs/qa_evidence/2026-09-22-model-switch-fixes/drive-result.txt
- manual: docs/qa_evidence/2026-09-22-model-session-audit/report.md

## Done means
- Guest role selections start the correct harness or explicitly refuse while preserving the active model and role.
- Direct model selections do not acknowledge an intermediate old model; picker logs record initial display, open rows and changes.
- Fallback failures retain each provider's reason, and known exhausted quota is not retried as transient.
- Targeted regression tests pass and an isolated live Relay drive records visible picker state and switching results.

## Plan
Goal: deliver the diagnosed selection and fallback fixes plus startup/change picker logging.
Findings: src/Pane.h sends role and model separately; backend/worker.py role switching bypasses guest startup; agent.py drops fallback error details; provider.py retries generic 429s.
Steps: (1) unify guest/API role switching and refusal state, remove intermediate Main switches; (2) instrument picker startup/open/pick/ack; (3) preserve failover failures and classify exhausted quota; (4) run focused tests, build via scripts/relay-build, drive isolated Relay under Xvfb and save screenshots/logs.
Risks: preserve conversation and pending mid-turn switches; shared checkout requires scoped commits. Exact rejected original pick remains unknown.
Verify: worker/guest/model-switch/failover/provider tests plus live startup, role/model changes, and a rejected switch.

## Execution Summary
Unified role and direct model transport changes, including guest startup and deferred changes; refused changes preserve the active provider and role. Removed intermediate Main switches. Added picker startup/open/pick/display logs and worker selection outcomes. Startup now displays Loading models… until the catalog arrives. Fallback diagnostics retain each provider's failure.

Verification: 299 targeted tests passed; 22 model-switch tests passed again after the final diagnostics addition; scripts/relay-build --target relay passed. Live rebuilt GUI/real-worker drive passed all 14 stages, including two new panes, guest/API switches, a refused guest, continued answers and quota/fallback errors. HTTP responses and guest harness were deterministic fixtures, not production provider calls. Screenshots, OCR, logs and reproducible driver: docs/qa_evidence/2026-09-22-model-switch-fixes/. Card test checks pass. Board-wide validation retains unrelated existing diagnostics; none apply to these two cards.
