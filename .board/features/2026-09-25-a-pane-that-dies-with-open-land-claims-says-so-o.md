---
id: R1QF
type: work
status: inbox
labels: [feature, workflow]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
source: '#3BM5 discuss, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [3BM5, FYEY], github: null}
---
# A pane that dies with open land claims says so on its card

## Issue
A crashed or closed turn can die between editing and committing (for example, the pane_send NameError in d2a60b55), and its land claims are then forgotten. `reap` keeps dirty sessions, but the card is not reliably told. When a pane goes away while it holds land claims with unlanded hunks, Relay should write a thread note on the claim's card listing the paths, so a later session can resume or drop the work. Lesson 3 from #3BM5.
