---
id: 74Y5
type: work
status: planned
labels: [feature, switchboard, qa]
component: [gui, worker]
parent: YZ8G
rank: zzzzzzzzzzzzzzzze
created: '2026-09-21'
source: 'owner, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [YZ8G, WC3E, JNYN], github: null}
---
# Built to be driven: the QA agent walks each step of the user experience, and apps expose a way to be driven by name

## Issue
how do i test out the new QA approach. is the agent going to design simulation? i think the QA agent should also always think about each step of the user experience , and apps should be built for live simulated drives by agents and humans -- the QA agent tries to optimize this to conserve scarce human tester time

## Done means
- The Verify brief (#WC3E) and the Try it brief (#JNYN) state the objective in one sentence: human tester time is the scarce resource; the agent plays everything it can and hands the person only the steps that need a person's judgement, with the minutes it will take stated.
- Before staging, the verifier writes the user's path through the change as numbered steps and marks each one `check` (a test or assertion decides), `agent` (the agent plays it and captures it), or `person` (only a person can judge); the record shows the three counts; a `person` step needs one line saying why an agent cannot judge it.
- Relay can be driven by name, not by pixel: a documented automation seam (the existing action registry, `app_action_run`, and the pane model) lets a driver open a card by id, press a named control on the card page or the tool row, read the notice line and a card's rendered sections, and type into a named box; `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ai-pass.sh` is rewritten to use it and stops carrying coordinates.
- A one-page design rule in the docs, for anything built with Relay: name every control, expose an open/press/read seam, keep fixtures disposable; the Verify brief points at it when the card is about an app.
Failure would show as: a brief asking a person to run tests or attach evidence; a driver that misses a click when a layout shifts; a verifier that stages before it has listed the user's steps.
