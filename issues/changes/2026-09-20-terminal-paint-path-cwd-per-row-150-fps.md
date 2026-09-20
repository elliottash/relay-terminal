---
id: 6W0Z
type: work
status: executing
labels: [bug, performance, engine]
assignee: claude-code
rank: m4
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-perf-profile/], related: [PF4K, 9MYY], github: null}
---
# Terminal paint path: /proc cwd lookup per painted row, 150 fps during output, full-width scrollback

## Issue
deploy opus subagents to profile and find performance issues and improvements. build it on sphinxpad as well to see how performs there

## Findings
Detail: [docs/qa_evidence/2026-09-20-perf-profile/engine/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/engine/FINDINGS.md) findings 1–4 and [docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md) finding 1 (two profilers found item 1 independently). libvterm core on both machines.

1. `restLinkColumns()` (`engine/view/TerminalView.cpp:1858`, from `paintRow`) calls `currentDirectory()` (`:1624-1641`) per painted row: a `readlink` + `stat` of `/proc/<pid>/cwd` and the core mutex the pty thread feeds under (`engine/session/TerminalSession.cpp:327-331`). 91–94 % of GUI-thread `statx`; 1,468/s streaming prose, 9,381/s in a tool-heavy turn; ~4,400 lock acquisitions/s. `hyperlinkAt(frameRow, col)` (`:1855`) converts every cell of the row to read one link id the caller already has — `vterm_state_convert_color_to_rgb` is the top self symbol at 10.3 %. A/B with colour-links off: 1.01 → 0.81 s GUI CPU per 50 MB of `cat`; 1.57 → 1.44 s for a streamed reply.
2. `scheduleFrame()` (`:452-454`) calls it a flood above 512 KiB since the last frame; at a 4 ms interval that needs 128 MiB/s and libvterm does 37 (spark) / 26 (sphinxpad). 590 `paintEvent` in 4 s by uprobe; ~1.0 s GUI CPU per 50 MB.
3. `engine/core/LibVtermCore.cpp:305-309` converts every scrolled line at full grid width and `pop_back`s blanks without shrinking: 25.6 MiB/s at 280x71 against 36.8 at 132x30; 2.8 kB per scrollback line (+27.7 MB Pss per 10k lines).
4. Any fold anchor on screen, collapsed ones included, forces a full-content repaint (`:493`): 4.8 rows per frame instead of 1.0. Agent panes always carry anchors (`src/Pane.h:9098`). Measured with a hand-printed OSC 8 anchor, not a real transcript.

## Plan
1. Resolve the directory once per frame in `pullFrame`; add a `VtCore::hyperlinkUri(id)` lookup. Expected 10–20 % of GUI CPU during output.
2. `flooding = bytes != m_bytesAtFrame` with a 16 ms interval, keeping 4 ms for the first frame after idle so key → paint (p50 10.6 ms) is untouched. Expected 1.0 → 0.42 s per 50 MB.
3. Find the last non-blank cell first, convert only up to it, `shrink_to_fit`: ~33–35 MiB/s wide, ~1.7 kB per line.
4. Gate the full repaint on `m_folds.expandedCount() > 0`.
GhosttyCore does not build on these machines: any `VtCore` interface change needs building elsewhere.
