---
id: 8EXS
type: work
status: ready
labels: [feature, sessions]
rank: zzzzzzzzzzzy
created: '2026-09-20'
source: 'split out of #XAME (pane /deliver, 2026-09-22)'
links: {plans: [], commits: [], evidence: [], related: [XAME], github: null}
---
# Sessions manager should also survive a restart

## Issue
when you close and re-open, if the options (or other menu) was open, it should re-open where you were

## Notes
Split out of #XAME, which made the Options and Actions panes survive a restart (`settings` node in the saved layout, restored by `RelayWindow::buildNode`). The Sessions manager (`Ctrl+Shift+Y`, `ToolPane::Kind::Sessions`) is still transient — `ToolPane::node()` returns `{}` for `m_hosted`.

Restoring it is more than the same three lines: creation and wiring live inline in `RelayWindow::openSessionsFor` (helper panel, closed-list feed, `bindSessionManager` to an owner pane, resume/close callbacks), so a restored pane needs that factored into something `buildNode` can call once the pane is placed and an owner pane can be found in the page. The node shape would mirror `subagents`/`internals`: `{"sessions": {"cwd": ..., "query": ...}}`, bound to a neighbour via a queued link. That function was contested by another session on 2026-09-20, which is why this is its own card.
