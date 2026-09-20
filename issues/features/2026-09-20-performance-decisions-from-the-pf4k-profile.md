---
id: GMCF
type: work
status: executing
labels: [feature, performance]
assignee: claude-code
rank: m8
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-perf-profile/REPORT.md], related: [PF4K, TZWF, 7M6E, 057J], github: null}
---
# Five performance decisions from the #PF4K profile

## Issue
i want all of these fixes.

explain the decisions and your recs

## Context
The seven fix cards from #PF4K are being implemented (owner: "i want all of these fixes"). These five
items in `docs/qa_evidence/2026-09-20-perf-profile/REPORT.md` ("For the owner to decide") are not
faults with one right answer; each trades something. The questions and recommendations are in the
thread.

## Decisions
Owner, 2026-09-20, answering the five questions in the thread by number:

1. Worker per pane, started lazily: "1 ok. 220MB seems like a good trade for the isolation"
2. Slimmer system prompt: "2 yes, and you can also deploy a fable subagent to propose further pruning / distillation of the system prompt components. or have a short version for local models / short context windows"
3. Keep the session file format, coalesce the saves: "3 ok"
4. Qt6 CI job now, keep shipping Qt6 on 26.04, flip CMake AUTO after #7M6E is verified on Qt6: "4 ok"
5. Asynchronous `systemd-run` probe: "5 yes"

And: "if there are other useful findings or fixes or tweaks, go ahead and do them"
