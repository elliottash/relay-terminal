---
id: R2Z3
type: work
status: discussing
labels: [bug, worker]
rank: zzzzzzzzzzzzzzzz
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# command_output loses finished jobs silently: "No command 'job-10'" after KEEP_FINISHED pruning

## Issue
i just saw this tool call error in the #97EG session:

▸ read job-10 output ✗ · No command 'job-10'. Job ids come from a run_command result ("job_id"); jobs end with the conversation.

is that a bug or something we shoudl address? because job-10 did exist in the session
