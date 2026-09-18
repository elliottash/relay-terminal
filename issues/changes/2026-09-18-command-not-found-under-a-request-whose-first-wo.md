---
id: W954
type: work
status: inbox
labels: [bug]
rank: zzzzzw
created: '2026-09-18'
source: issues/bug_intake.txt, 2026-09-18
links: {plans: [], commits: [], evidence: [], related: [T4JV], github: null}
---
# "command not found" under a request whose first word ends in a comma

## Issue
another "command not found" bug:
✦ yeah, see if there is a clear issue to resolve. if not, lets unblock and start backfilling at full capacity
command not found: yeah,

Note from filing: reproduced against `relay_core.router.explain_invalid` — it answers True for this line and False for the same line without the comma. #T4JV's rule "a name that is not a plain lowercase word" counts `yeah,` as evidence a command was meant; sentence punctuation on the first word (`,` `.` `:` `?` `!`) should not.
