---
id: 308N
type: work
status: ready
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
rank: zz31
created: '2026-09-17'
labels: [bug]
acceptance: a suggestion appears after a finished command and can be accepted with Tab, with a test covering the path
source: '`issues/feature_intake.txt`, 2026-09-17: "suggested next command / prompt isnt working yet"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Next command and next prompt suggestions never appear

The suggestion feature (ghost text in an empty prompt box after a command, `requestSuggestion("next_command", …)`)
does not produce anything in normal use. Find out where it stops: the setting, the side call, the model role
(suggestions now run on the Flash tier), the event, or the ghost-text path in the composer. Add a test that
would have caught a silent failure, and a status line when a suggestion call errors.
