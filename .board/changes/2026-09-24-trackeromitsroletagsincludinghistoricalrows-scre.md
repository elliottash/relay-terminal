---
id: HYBM
type: work
status: inbox
labels: [bug]
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# trackerOmitsRoleTagsIncludingHistoricalRows screenshot test flakes in full-suite runs

## Issue
Unrelated fault noticed while landing #M5FZ: SubagentsTests::trackerOmitsRoleTagsIncludingHistoricalRows is flaky — it failed in one full-suite run of ./build/relay-subagents-tests (Compared QImages differ in size: actual historical 300x153 vs expected general 1499x153, tests/subagents_test.cpp:197) and passed when run alone and in the two following full-suite runs, same binary, no source change involved. Screenshot-comparison test on the subagent strip; looks order/timing dependent (a prior test's widget width leaking in).
