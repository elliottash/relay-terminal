---
id: 6W0Z
type: work
status: needs-verification
labels: [bug, performance, engine]
assignee: claude-code
rank: m4
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [b8e91fe3], evidence: [docs/qa_evidence/2026-09-20-perf-profile/, docs/qa_evidence/2026-09-20-perf-fixes/engine/], related: [PF4K, 9MYY, PPR4], github: null}
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

## Done — b8e91fe3

All four, plus #PPR4's item 2 (`resolveFoldAnchors` / `retainAnchored`), which lives in these same
files. Before/after on spark with the profiler's own harness, measured against **the same tree with
and without this commit's hunks**: [docs/qa_evidence/2026-09-20-perf-fixes/engine/RESULTS.md](../../docs/qa_evidence/2026-09-20-perf-fixes/engine/RESULTS.md).

| | before | after |
|---|---|---|
| GUI CPU per 50 MB of `cat` | 1.18 s | **0.65 s** |
| paints per second during a flood | 167 | **60** |
| GUI-thread `statx` / `readlinkat` per second | 6 597 / 6 161 | **494 / 65** |
| scrollback per line (79-char lines) | 3.01 kB | **1.67 kB** |
| `cat` 50 MB at 281x72 | 26.4 MiB/s | **32.7 MiB/s** |
| `seq 1 2000000` | 2.36 s | **1.21 s** |
| byte → first paint (p50) | 5.2 ms | 4.8 ms |

Two corrections to the findings above, both measured here:

- **Finding 4's mechanism was mis-stated.** A *collapsed* anchor never forced the full repaint:
  `FoldLayer::active()` is already "an expanded fold, or a prose block that has taken its rows
  over" (`rebuildAnchors` skips everything else). What an agent pane really sits in is the second
  case — every prose block (#R2WQ) takes its rows over as soon as the pane is any width but the one
  it printed at — and *that* is what made every frame repaint every content row: measured 1.0 rows
  per frame with a bare anchor, 10.0 with a fold open, 11.1 with a taken-over prose block. The fix
  is therefore not `expandedCount() > 0` but mapping the dirty-row region through the fold layer,
  which everything that moves a block already forces a full frame past. Now 2.0 and 1.1.
- **#PPR4 item 2 did not flatten the per-turn curve.** The two walks are provably cheap now
  (`retainAnchored` 0.07 %, the ring walk 0.11 % of GUI cycles 200 turns in; unit tests pin the
  operation counts), but GUI CPU per turn still goes 61.8 → 78.2 ms over 250 turns, before and
  after alike. The growth is elsewhere, in `src/` per-turn bookkeeping; the profile is on #PPR4's
  thread for whoever holds its item 1.

`VtCore` gained one method, `hyperlinkUri(id, row, col)`, whose **default implementation is
today's `hyperlinkAt(row, col)`** — so `GhosttyCore`, which builds on neither of these machines,
compiles and behaves exactly as before and can be given the direct lookup by whoever can build it.
`LibVtermCore::hyperlinkRuns` caches the scrollback half of its walk; a core that does not is
correct, only slower.

## QA checklist

Nothing should look different; the change is in *when* rows are repainted and how often frames go
out. What to glance at (an agent pane, a real transcript):

- [ ] A streaming agent reply still repaints cleanly at the bottom of the pane — no half-drawn or
      stale rows left behind — and looks the same after the pane has been resized once (which is
      what puts its prose blocks into the re-wrapped state this change touches).
- [ ] Open a tool-call fold, scroll inside it, shut it: the rows under it close up with nothing
      stale on screen. (Automated: `engine/scripts/gui/folds.sh` against this build and the one
      before it gives pixel-identical screenshots in all eight states.)
- [ ] `cd` in a pane, then `ls` — paths in the output still wear the link colour, resolved against
      the new directory (the directory is now read once a frame instead of once a row).
- [ ] Typing echo still feels immediate in a terminal pane (byte → paint p50 4.8 ms, measured).
