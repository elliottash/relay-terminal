---
id: MDSG
type: work
status: planned
labels: [bug, performance]
assignee: null
rank: m5
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-perf-profile/], related: [PF4K], github: null}
---
# Sessions search costs 100–190 ms per key; a 15,000-line file freezes the file pane for 2 s

## Issue
deploy opus subagents to profile and find performance issues and improvements. build it on sphinxpad as well to see how performs there

## Findings
Detail: [docs/qa_evidence/2026-09-20-perf-profile/board/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/board/FINDINGS.md), findings 4 and 5.

1. Sessions search-as-you-type on a read-only copy of the owner's store (863 conversations, 115 MB `index.db`): **100–190 ms GUI CPU per key**, 300–900 ms wall, while the pane prints "40 ms" for its query. No SQLite frames in `perf`: `RowDelegate` builds a `QTextDocument` and calls `setHtml` per row twice, in `sizeHint` (`src/Conversations.cpp:448-451`) and `paint` (`:486-488`), for ~100 rows on every `rebuildTree` (`:1287`). (The SQL itself: 2–40 ms here; the worker profiler saw 76 ms for a one-word search.)
2. Opening `src/Pane.h` (15,830 lines) in the file pane: **1,965 ms cold / 688 ms warm** GUI freeze, +58 MB RSS. The same bytes as `.txt` take 169 ms, so ~520 ms is KSyntaxHighlighting: `setDefinition` rehighlights every block synchronously (`src/FilePanes.cpp:1312/1316/1320`, also `:1130/1134`, `:1646/1650`).

## Plan
1. Cache the laid-out document per (html, width), cleared on refill, and keep the row height in item data. Colour already comes from `PaintContext`, so the cache is safe; bound its size. Expected ~4× (30–40 ms per key).
2. Install the highlighter after first paint and rehighlight in chunks from the top on idle; cancel on document replace (the editor pane); skip highlighting above ~2 MB. First paint 1,965 → ~200 ms.
