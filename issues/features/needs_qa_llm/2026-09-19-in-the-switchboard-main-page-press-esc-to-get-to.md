---
id: K9X6
type: work
status: needs-qa-llm
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzy
created: '2026-09-19'
links: {plans: [], commits: [b15c86e3, 5405cc1c], evidence: [docs/qa_evidence/2026-09-19-esc-to-filter/], related: [], github: null}
---
# in the switchboard main page, press esc to get to the filter bar

## Issue
in the switchboard main page, press esc to get to the filter bar

## QA checklist
- [ ] On the Switchboard main page (list page, no card open), Esc puts the keyboard in the filter bar, ready to type.
- [ ] Precedence holds: an open card closes first; an active filter clears first (matches come back, focus stays on the list); Esc in an empty filter still returns to the list.
- [ ] Clicking into the filter with the mouse hints "Next time: Esc" (once, honouring the ShortcutHints gates); `n` / `/` hints still work.
- [ ] The key line at the bottom of the list page reads "/ or Esc filter".
- [ ] `relay-board-tests` green (57 passed, incl. the new `escOnTheMainPageGoesToTheFilterBar`); `boardsections`, `boardworkspace` green — see `docs/qa_evidence/2026-09-19-esc-to-filter/`.
- [ ] Optional live check under Xvfb: focus arrival in a real window, and Esc while a section checkbox has the keyboard (same `handleBoardKey` path).

QA recommendation: run on a Flash-family model. Implementer was glm/glm-5.3 (commits b15c86e3, 5405cc1c).
