---
id: 1Q2F
type: work
status: done
labels: [bug, backend]
implemented_by: glm/glm-5.3
verified_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-22'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# worker.py protocol-error handler crashes on malformed message: bare `kind` at line 763

## Issue
Unrelated fault found while delivering #CP3M. `python3 -m unittest tests.test_agent.WorkerTests.test_malformed_request_does_not_crash` fails on a clean checkout: backend/worker.py's exception handler references bare `kind` at line 763 (`if kind in ("set_model", "set_agent_role") …`), but a malformed message (e.g. `[]`) raises ValueError("Protocol message must be an object.") at line 217 before `kind` is assigned, so the handler itself dies with UnboundLocalError and the worker exits 1 instead of reporting the protocol error. Neighbouring lines in the same handler already use `locals().get("kind")`; line 763 does not. Traceback: worker.py line 217 ValueError → line 763 UnboundLocalError. worker.py is unmodified vs HEAD; pre-existing.
