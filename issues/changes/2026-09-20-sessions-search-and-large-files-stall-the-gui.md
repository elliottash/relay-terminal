---
id: MDSG
type: work
status: needs-verification
labels: [bug, performance]
assignee: claude-code
rank: m5
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-perf-profile/, docs/qa_evidence/2026-09-20-perf-fixes/panes/], related: [PF4K], github: null}
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

## What was done
Numbers, commands and the harness: [docs/qa_evidence/2026-09-20-perf-fixes/panes/MEASUREMENTS.md](../../docs/qa_evidence/2026-09-20-perf-fixes/panes/MEASUREMENTS.md). Before and after are the same bench code run against a clean export of `main` and against this tree; no part of it reads the owner's store.

1. **Sessions, one keystroke: 392 → 62 ms GUI CPU** with every row unfolded, **37 → 4.4 ms** with them collapsed (Xvfb/xcb; offscreen 127 → 12 ms and 36 → 4.1 ms). The delegate's `QTextDocument` was a third of it and is now kept in a bounded `RichTextCache` keyed by (html, width, font). The other two thirds were not in the delegate at all: `setFirstColumnSpanned()` on a row already in the tree, and every `setExpanded()` restoring an unfolded row, each make Qt re-measure the three `ResizeToContents` columns over every row — about a hundred full walks per page. The spans are now set once at the end of the fill, and the three columns keep their width while it is filled and are measured once over the finished tree. **The pixels are unchanged**: the same page rendered by both builds is byte-identical under `xcb` and offscreen.
2. **File panes, a 1 MB `src/Pane.h`: 203 → 51 ms of GUI CPU before the text is on screen**, and 120 → 33 ms for the 419 kB file the finding names. The highlighter is installed on the filled document and works down it in 4 ms slices, strictly top down so a block's state is always its predecessor's; the first slice covers 400 blocks, so a file of a few hundred lines is finished before the pane is painted and nothing small flickers. A host's file over 2 MB is shown plain (a local one is already capped at that by `readCapped`). Deleting the highlighter — what every pane does when it loads another file — stops it.

Not done, and why: `rebuildTree` still runs on every keystroke. It now costs 4 ms, the search is already debounced at 120 ms, and the worker's reply differs on nearly every key because the match lines do, so comparing result sets would cost more than it saves.

Note for the owner, not changed here: the sessions status line ends with a bare `· 40 ms`, which is `elapsed_ms` from the worker — the search itself, not the redraw that follows it. It was never wrong, but it is what made this one hard to see. Whether it should say what it is timing is a wording decision.

## QA checklist
- [ ] Open the sessions pane (`/resume` or Ctrl+Shift+M) on a store with a few hundred conversations and type a word one letter at a time. The list keeps up; the rows, the tags, the "Updated / Turns / Model" column widths and the quick-look text look exactly as they did.
- [ ] Unfold several session rows, then type another letter. The rows stay unfolded, the same row stays selected and the list stays where it was scrolled to.
- [ ] Change the theme (Options › Appearance) with the sessions pane open and search again: the match lines and summaries are in the new theme's colours, not the old ones.
- [ ] Narrow the pane until the list is half its width and search again: the rich-text rows re-wrap to the new width.
- [ ] Open `src/Pane.h` in a file pane. The text is there almost at once, the first screenful already coloured. **This is the one visible change:** scroll or jump to the bottom of a file that big within the first second and the lines there are plain for a moment before the colours catch up. Nothing may ever be coloured *wrongly* — no line should look like it is inside a comment or a string when it is not.
- [ ] Open a 300-line file: it is coloured the instant it appears, with no flicker.
- [ ] Open a big file and immediately open another one. The second file colours normally and nothing of the first one's colouring lands in it.
- [ ] Open a plan (the editor pane), type in it, and check the colouring follows what you type.
- [ ] Open a file on a host (`ssh://…`) and check it still colours and still saves.
