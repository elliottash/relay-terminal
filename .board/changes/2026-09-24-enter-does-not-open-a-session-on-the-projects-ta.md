---
id: 0VM0
type: work
status: needs-verification
labels: [bug, sessions, keyboard]
assignee: agent
implemented_by: glm/glm-5.3
session: 4844fb71-f91f-4916-8a1c-fade1c3bf01e
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: Enter on a session row calls onResume with that session; Enter on a project row only folds it; the projects/conversations suites pass, sign_off: none, effort: low}
source: pane 1, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-projects-tab-enter-resumes/], related: [], github: null}
---
# Enter does not open a session on the Projects tab of the Sessions pane

## Issue
bug: pressing enter on a session in the sessions pane didnt take me to it, i had to press the resume button

## Done means
On the Projects tab of the Sessions & Projects pane, selecting a session row and pressing Enter (or Enter from the search box after filtering) goes to that session — the same thing the "Go to session" button does. Enter on a project row only folds/unfolds it. Up/Down leave the search box into the tree, Esc clears the query, `/` returns to the search box.

## Tests
`relay-projectspane-tests` (6 passed): new `enterResumesSessionWithoutTheButton` — Enter on the session row fires `onResume` with the session, Enter from the search box does the same, Enter on a project row toggles expansion and fires nothing. Sibling surface `relay-conversations-tests`: 53 passed, 2 skipped (Sessions tab keyboard unchanged).
