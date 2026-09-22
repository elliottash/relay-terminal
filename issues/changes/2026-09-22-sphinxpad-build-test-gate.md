---
id: SBT2
type: work
status: discussing
labels: [bug, tests, build]
rank: msbt2
created: '2026-09-22'
source: Found during SPB2 package validation by Codex, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-sphinxpad-build/], related: [SPB2, 3BPH, 99T0], github: null}
---
# Sphinxpad package gate fails on clean main

## Issue
During the requested fresh build on sphinxpad, clean revision fff7eb8fdf4617e2cc845805c209f32e264dd77d compiled successfully with Qt6 and system Python 3.14 on Ubuntu 26.04 amd64, but the test gate did not pass.

## Tests
- `boardworkspace`: anOptionOrSessionLinkOpensWhereItNames fails because its source assertion cannot find Pane::openOutputTarget().
- `boardexecute`: three missing-button failures; already tracked by #3BPH.
- `backend-and-bash`: timed out at 600.23 seconds, with earlier FAIL results in malformed-request handling, two guest-delegation cases, and the browser install-prompt case. Automatic repeat stopped after the first timeout; related timeout history is #99T0.
- `relay-engine-tests`: separate run failed; exact cases are recorded in engine-test.log.
- Evidence: docs/qa_evidence/2026-09-22-sphinxpad-build/build-system-python.log and engine-test.log.

## Done means
- Diagnose the reported failures as product regressions, stale tests, or environment requirements, with evidence.
- The native package gate finishes successfully with system Python 3.14.
