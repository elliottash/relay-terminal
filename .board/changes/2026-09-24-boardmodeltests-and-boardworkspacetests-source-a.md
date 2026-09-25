---
id: KQ1T
type: work
status: needs-verification
labels: [bug, board, tests]
assignee: agent
implemented_by: glm/glm-5.3
session: 15e42790-4cc5-403f-9602-bc49a74c6e67
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'ctest -R ''^(board|boardworkspace)$'' passes on a build of current main, with assertions that hold against committed HEAD source text', sign_off: none, effort: medium}
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# BoardModelTests and BoardWorkspaceTests source assertions stale since #AGNT

## Issue
claim and deliver all, and clean out the stale tests

## Done means
`ctest -R '^(board|boardworkspace)$'` passes on a build of current main. The ten stale functions assert the post-#AGNT console architecture (card consoles, deliverToConsoles, current wiring and labels), and every source-text assertion they keep holds against `git show HEAD:<file>`, not uncommitted working-tree text.
