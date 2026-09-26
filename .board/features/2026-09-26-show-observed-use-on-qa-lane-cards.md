---
id: SWQN
type: work
status: planned
labels: [feature, qa, switchboard]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
links: {plans: [], commits: [], evidence: [], related: [YZ8G, P7CF], github: null}
---
# Show observed use on QA lane cards

## Issue
Use local run history to show how often a QA-lane card was exercised over a period and whether captured faults occurred. Keep this evidence informational; it must never auto-close a card.

## Plan
Read locally recorded Try it and Review runs by card and time; define a stable count and fault attribution; display the count beside verification evidence; test empty, repeated and fault-bearing histories. Keep this independent of the close gate.

## Done means
A QA-lane card shows an information-only count of observed local uses over a stated period and captured faults; zero faults never changes its status automatically.
