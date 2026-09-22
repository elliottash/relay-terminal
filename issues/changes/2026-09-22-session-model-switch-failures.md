---
id: MSW7
type: work
status: discussing
labels: [bug, models, guest]
rank: zmsw7
created: '2026-09-22'
source: Codex session analysis in a Relay pane, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-model-session-audit/], related: [4BPE, YJG7, GH5T], github: null}
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
- manual: docs/qa_evidence/2026-09-22-model-session-audit/report.md — log analysis and no-network Python reproductions.
