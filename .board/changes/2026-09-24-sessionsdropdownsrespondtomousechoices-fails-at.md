---
id: 9ESY
type: work
status: inbox
labels: [bug, sessions, tests]
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
source: Relay pane, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [MXMG], github: null}
---
# sessionsDropdownsRespondToMouseChoices fails at HEAD: group combo will not re-open to \"none\"

## Issue
Found while closing out #MXMG: ConversationsTest::sessionsDropdownsRespondToMouseChoices fails reproducibly — `chooseComboItem(group, "none")` at tests/conversations_test.cpp:1609 returns false — in the current tree AND in a clean export of HEAD (built fresh in /tmp, so it predates #MXMG's uncommitted changes). Not filed by the session that introduced it.
