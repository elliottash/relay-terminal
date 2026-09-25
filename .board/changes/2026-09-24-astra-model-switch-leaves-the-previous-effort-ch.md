---
id: 2RYC
type: work
status: needs-verification
labels: [bug, models, ui]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: a85a15ff-d83e-4ebb-8e29-95d650fb6dd3
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [person], human: optional, criteria: Switch GLM Main → Astra High → Sol and confirm the menu follows each active model., sign_off: none, effort: medium}
source: Relay pane, 2026-09-24
links: {plans: [], commits: [9314675960b2f2e6b4b1734d5720a76cca28c775, 569116039bdfe7debfcfa83bf065641428f13db2], evidence: [docs/qa_evidence/2026-09-24-2RYC/consolemode.txt], related: [], github: null}
---
# Astra model switch leaves the previous effort choices

## Issue
apparent bug: i just had a pane with glm 5.3 in it, which ad mits low high max effort. i switched to gpt-6-astra and it didnt cahnge the effort options

## Done means
After a pane changes from GLM Main to Astra in High, its effort menu offers Astra's levels, including medium and xhigh, and no longer offers GLM's three-level list. Returning to Main restores GLM's levels. The selected effort is valid for the active model.

## Tests
- `ctest -R consolemode` — tests/consolemode_test.cpp

### Check
Pass — the exact landed tree built and passed the focused test for commits 9314675960b2 and 569116039bdf. Output: docs/qa_evidence/2026-09-24-2RYC/consolemode.txt. The shared checkout's fast build could not link because another session's unfinished PaneDirectory symbols were present there.

### Check 2026-09-24 18:49
- passed · ctest:consolemode — ctest -R consolemode passed for this revision on spark-dcc9, 2026-09-24T22:49:05Z
- notice · ctest:consolemode — ctest -R consolemode is slow: p95 6.69 s, p50 1.62 s
history: thread
## Execution Summary
The effort menu now resolves the active High/Flash role's model before falling back to the pane's Main model. This fixes GLM Main → Codex Astra High showing GLM's three levels. Regression coverage checks GLM Main → Astra High → Sol High → GLM Main. Code landed in 9314675960b2 and the test cleanup in 569116039bdf.
