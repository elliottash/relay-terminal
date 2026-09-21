---
id: WRWN
type: work
status: done
labels: [bug, switchboard]
implemented_by: glm/glm-5.3-flashx
verified_by: glm/glm-5.3-flashx
rank: zzzzzzzzzzzzzzzw
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# relay-boardmodel-tests does not compile at tip: onModelPick void lambda vs bool signature

## Issue
scripts/relay-build fails at tip: tests/boardmodel_test.cpp does not compile (onModelPick void-vs-bool). Found while building for #48S3; not my files, not touched by me.
