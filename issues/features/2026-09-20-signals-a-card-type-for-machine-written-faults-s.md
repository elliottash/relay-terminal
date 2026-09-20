---
id: AQ6X
type: work
status: discussing
labels: [feature, switchboard, tests]
waiting_on: agent
rank: i1
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [], evidence: [], related: [R9G7, 7BM4], github: null}
---
# Signals: a card type for machine-written faults such as failed tests

## Issue
do you think there should be a separate "agent-only" card type, that is for tracking, but humans arent supposed to read it.

like, cards that come from failed tests

the signals feature seems like it requires more investigation. so can you do more research on similar features in other harnesses / systems / project managers to firm it up? then we can write a detailed card under discussion. we can also connect it with #7BM4 as desired

## Proposal so far (2026-09-20, before the research)
A `signal` card type for faults a machine writes and a machine closes, because its lifecycle differs from a work card's:
- own folder `signals/`, two states (open, resolved); never synced to GitHub, never surveyed, never imported;
- keyed, not filed: one signal per key (test name, or suite plus first failing assertion); a rerun adds an occurrence; a mass failure in one run collapses into one signal;
- auto-resolved after two consecutive green runs; resolved signals age out like retired memory cards;
- folded for humans: a count on the bugs tab and one expandable row (the same fold as the self-closed done cards);
- promotion is the escalation: when the agent cannot fix it, or it is older than a day, the agent files a real bug card linked to the signal, and the signal stays open until that card closes;
- `session` claims work on a signal as on a card, so two panes do not chase the same red test.

Research is under way (docs/SIGNALS-RESEARCH.md); this card is rewritten with its findings and numbered questions for the owner when it lands.
