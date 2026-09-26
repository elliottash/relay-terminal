---
id: 3S34
type: work
status: done
labels: [bug, qt6, packaging]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
verified_by: openai/gpt-6-sol via codex
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low, stakes: rework, blast: capability}
links: {plans: [], commits: [4b4dd33621a3], evidence: [], related: [9Y7X, WV4V], github: null}
---
# Board view uses QSet::fromList, which blocks Qt 6 package builds

## Issue
The clean Ubuntu 26.04 `.deb` build from commit 0d5ac28e configures Qt PDF but fails compiling src/BoardPane.cpp:2195: QSet<QString>::fromList is unavailable in Qt 6. Replace it with a Qt 5/6-compatible construction and confirm a Qt 6 build.

## Done means
The Board view's default hidden sections compile under both Qt 5 and Qt 6, and the Ubuntu 26.04 package build passes this source file.

## Tests
`ctest -R boardpane` — tests/boardpane_test.cpp

### Check
PASS — an isolated Qt 6 build compiled `relay-boardpane-tests`, then `ctest -R '^boardpane$'` passed. The clean Ubuntu 26.04 package build had previously failed on `QSet::fromList` at `src/BoardPane.cpp:2195`.

### Check 2026-09-26 00:33
- passed · ctest:boardpane — ctest -R boardpane passed for this revision on spark-dcc9, 2026-09-26T04:33:22Z
- notice · ctest:boardpane — ctest -R boardpane is slow: p95 0.80 s, p50 0.35 s
history: thread
## Execution Summary
Replaced the Qt 5-only `QSet::fromList` call with three `insert` calls in `src/BoardPane.cpp` (commit `4b4dd33621a3`).
