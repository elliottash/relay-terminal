---
id: CBE6
type: work
status: inbox
labels: [feature, workflow, board]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: '#3BM5 discuss, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [3BM5, FYEY], github: null}
---
# Board tool writes land on their own instead of piling up uncommitted

## Issue
The board_* tools write .board/ files, but nothing commits them, so dozens of card, thread and cases.jsonl edits from every session sit dirty in the checkout. `land.py board-sync` exists, but someone has to run it by hand. Board writes should land automatically, either on each write or in a periodic sync. Lesson 2 from #3BM5.
