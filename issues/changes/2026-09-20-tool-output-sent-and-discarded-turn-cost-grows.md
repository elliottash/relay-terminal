---
id: PPR4
type: work
status: planned
labels: [bug, performance]
assignee: null
rank: m6
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-perf-profile/], related: [PF4K], github: null}
---
# Tool output is sent to the GUI and thrown away; per-turn GUI cost grows with the conversation

## Issue
deploy opus subagents to profile and find performance issues and improvements. build it on sphinxpad as well to see how performs there

## Findings
Detail: [docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md), findings 2, 3 and 5. Stub provider, spark and sphinxpad.

1. `tool_output` (40.5 %) and `tool_result` (40.9 %) are 81 % of worker → GUI bytes, ~66 KB per tool call in the test. In the default configuration the GUI's only use is `m_toolLines += text.count('\n')` (`src/Pane.h:9699`); `result.output` is never read, and folds fetch their text separately. `QJsonDocument::fromJson` is 7.1 % of GUI cycles; about 1 s of a 6.8 s tool-heavy turn.
2. GUI cost per turn grows: 65 ms at turn 25 → 86 ms at turn 225 of a 300-turn conversation. `resolveFoldAnchors` runs `hyperlinkRuns` — a full-scrollback cell walk whose own comment says it is for resize/trim/clear — on every block close and on a 250 ms heartbeat; `FoldLayer::retainAnchored` uses `QVector::contains` (O(folds²)) and calls `rebuildAnchors()` per anchor.
3. One 65–81 ms event-loop stall at the start of each turn. Otherwise p99 is 2 ms over 35k iterations.

## Plan
1. A `set_agent_options` flag so the worker sends a line count unless "show tool output" is on. This is a protocol change: `docs/AGENT-SESSIONS-PROTOCOL.md` and the remote/phone view need to agree.
2. `QSet` in `retainAnchored`, one `rebuildAnchors()` per batch, incremental `hyperlinkRuns`, and no heartbeat while no fold is expanded.
Not measured: the Activity pane's `setThinking` re-renders the whole reasoning block (up to 400 KB of markdown) at 4 Hz — read in the code, but the pane could not be opened deterministically from the harness.
