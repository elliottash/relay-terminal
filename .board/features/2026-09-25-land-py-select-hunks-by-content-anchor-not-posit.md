---
id: NPCD
type: work
status: inbox
labels: [feature, workflow]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
source: '#3BM5 discuss, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [3BM5, R5TC], github: null}
---
# land.py: select hunks by content anchor, not position

## Issue
`--only-hunk` and `--exclude-hunk` take hunk numbers, and the numbers shift with diff context and with other sessions' edits. In #R5TC, a selection numbered at -U1 was applied at -U3, so the wrong hunk was left out and the commit message described a row that never landed. Add content-addressed selection (`--only-hunk <path>:<anchor text>`) so a selection survives renumbering. Also let one commit carry a second card's tag when a single hunk holds two cards' work. Lessons 7 and 8 from #3BM5.
