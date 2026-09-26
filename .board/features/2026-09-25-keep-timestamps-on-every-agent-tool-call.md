---
id: BXF1
type: work
status: needs-verification
labels: [feature, agents]
assignee: agent
implemented_by: openai/gpt-6-luna via codex
session: a58f57d8-0c3f-4b61-963e-66a44404a577
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: person, also: [script], human: required, criteria: 'Check running and settled tool rows in the main terminal, Activity view, and subagent transcript; each settled row ends in ` · HH:MM:SS` for when it was written.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: pane, 2026-09-25
links: {plans: [], commits: [919a83b82e2a, '0bbb88ab6092', 24c90685106b], evidence: ['src/CallLines.cpp (commit 919a83b82e2a; build: python3 scripts/land.py try tooltimestamps)', 'src/PaneEvents.cpp (commit 0bbb88ab6092; build: python3 scripts/land.py try tooltimestamps-edges)', 'src/CallLines.cpp (commits 919a83b82e2a, 0bbb88ab6092, 24c90685106b)'], related: [], github: null}
---
# Keep timestamps on every agent tool call

## Issue
Show each tool call's local display time on its settled row across main agent and subagent views.

> we added timestamps at the last tool call. but i think we should just put it on all of them. in main agents and subagents
> — elliott · [session:b564d233a60145ceb86e76382d1d118c](relay://session/b564d233a60145ceb86e76382d1d118c) · 2026-09-25

## Plan
**Goal**
Show the local time each tool-call row is written, with a seconds-level timestamp on main and subagent surfaces.

**Findings**
- `src/CallLines.cpp` stamps main terminal rows while running; settled rows need the result-display time.
- `src/AgentInternalsView.cpp` renders Activity rows independently.
- `src/SubagentTranscript.cpp` renders subagent rows independently.

**Steps**
1. Extend shared call-row formatting to append a local display time on running and settled rows.
2. Update Activity and subagent rows with the completion/display time when results arrive.
3. Preserve the latest represented call's display time when reads merge; inspect targeted diffs.

**Risks**
Merged tool calls share one row; that row shows the latest represented call's display time.

**Verify**
Review targeted diffs; a separate pass should run the call-line, Activity, and subagent tests and visually inspect all three surfaces.

## Done means
- Every tool call row appends its local display time as ` · HH:MM:SS` while running and after it finishes.
- A settled row shows the time it is written to the screen; the live row shows when it began.
- Main agent terminal rows, the Activity view, and subagent transcripts use the same timestamp format.
- Merged read rows stay a single row and show the latest represented call's display time.

## Decisions
The user specified the suffix: “just add " · HH:MM " at teach call” (interpreted as “each call”), then clarified “HH:MM:SS is better.” The settled tool-call row shows the local wall-clock time when the note is written to the screen, as asked: “i meant, when the ntoe prints to your screen, what time is that”. The running row keeps its start time.

## Execution Summary
Added HH:MM:SS display-time suffixes to settled tool rows in the main terminal, Activity view, and subagent transcript. Running rows retain their start stamp; merged read rows use the latest member's completion time. The exact claimed source tree built successfully with `python3 scripts/land.py try tooltimestamps`.
Also timestamped the deferred plain-text main-agent row and the Activity row stored for later replay to the terminal. Landed in `0bbb88ab6092` after the exact tree built successfully.
Updated existing call-line, Activity, and subagent row expectations for the appended suffix. Commit `24c90685106b` landed; the exact claimed source tree built successfully during landing.

## Tests
`python3 scripts/land.py try tooltimestamps` — passed (compile only).
`python3 scripts/land.py try tooltimestamps-edges` — passed (compile only).
`python3 scripts/land.py commit tooltimestamps-test-expectations -m "Update tool row timestamp expectations (#BXF1)" --paths tests/calllines_test.cpp tests/agentinternals_test.cpp tests/subagents_test.cpp` — passed (compile only).
`ctest --test-dir build -R 'calllines|agentinternals|subagents'` — not run in this turn.
