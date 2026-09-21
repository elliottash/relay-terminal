---
id: MDKN
type: work
status: executing
labels: [bug, terminal]
component: [gui]
workstream: terminal
assignee: agent
rank: m5
created: '2026-09-21'
acceptance: a markdown link's label opens what the link names, in a terminal pane and in an embedded console, before and after a re-wrap
source: "#AGNT QA checklist, 2026-09-21: \"A markdown link's label is not clickable\"; owner, 2026-09-21: \"yes, add the linking\""
links: {plans: [], commits: [], evidence: [], related: [AGNT, R2WQ, FEJQ], github: null}
---
# A markdown link's label is not clickable

## Issue
A markdown link's label is not clickable — only the target printed beside it is.

Owner, 2026-09-21, on whether to do it: **"yes, add the linking"**.

`MarkdownAnsi` renders `[LABEL](target)` as `LABEL (target)`, the label in the `[ui] link` ink —
underlined dark green, the ink every clickable thing in the transcript wears. Only the `(target)`
printed after it is clickable, because that is text and `relay::links::candidates` scans text.
Two automated drives clicked the label and read the result as the feature working; so will a
person, because the label is painted like a link.

## Decisions
- **The label carries the target as an OSC 8 run inside the prose run** — a run whose URI is the
  block's own anchor with the target as its fragment (`relay://prose/<pane>/<n>#l=<target>`).
  OSC 8 runs do not nest, but they do not have to: the pane already switches the anchor mid-line
  and switches back, for the `#K7Q2` segment of a tool-call row (`Pane::drawCallRow`, card #1NW3).
  The fragment keeps the label's cells inside the block's own URI namespace, so
  `hyperlinkRuns(kProsePrefix)` still returns them and the block's row range is whole even when a
  label is the only thing on its first or last grid row.
- **The printed `(target)` stays, for every kind.** It is what a person reads before clicking, and
  it is the fallback that survives a restore from the saved terminal bytes, where OSC 8 is
  stripped (`Pane::sanitizeSgrOnly`). Dropping it for `option:`/`session:`/`card:` would make a
  restored transcript's relay links unreachable rather than merely unhighlighted. It also makes
  the change additive on screen: a transcript renders to exactly the same pixels as before.
- **The keyboard walk (Ctrl+Shift+L) is left alone.** Because the target is still printed, every
  label's destination is already one stop on the walk; adding the label would give every markdown
  link two stops for one destination.

## Planning notes

### Cause
`src/MarkdownAnsi.cpp:407-428` emits the label as coloured text and nothing else. The hit test
(`TerminalView::linkAt`) has two ways to find a link — an OSC 8 run on the cell, or a scan of the
row's text — and the label is neither: the only OSC 8 on that cell is the prose anchor, which
`linkAt` explicitly refuses ("A prose anchor (#R2WQ) is not a link"), and the label's *text* is
prose, not a path.

### The change
- `engine/TerminalBackend.h` — the `#l=` fragment convention beside `kProsePrefix`, with
  `proseLinkUri()`, `isProseLinkUri()`, `proseLinkTarget()` and `proseBlockUri()`.
- `src/MarkdownAnsi.{h,cpp}` — `setLinkAnchor()`. Empty (the default) and the renderer emits
  exactly the bytes it did before, which is what every surface that is not a pane transcript
  wants. Set, and a link's label is wrapped in the OSC 8 run and the anchor re-opened after it.
  `visibleWidth()` also learned to skip an OSC sequence, so a link in a table cell still measures.
- `engine/view/ProseSpans.h` — `ProseCollector` reads OSC 8 and puts a *link* URI (never an
  anchor) on the span, so a re-wrapped prose block and a markdown fold carry the label's target in
  `FoldSpan::link`, which `FoldLayer` and `TerminalView::foldLinkAt` already paint and hit-test.
- `src/CallLines.cpp` — `appendMarkdown` reads OSC 8 the same way (it used to leave the sequence's
  body in the row's text), and `FoldOptions::linkAnchor` turns it on for a thinking bubble.
- `engine/view/TerminalView.cpp` — the pieces of one block's anchor run are merged by block URI
  before they reach the fold layer; `linkAt` resolves a `#l=` URI through `relay::links` exactly
  as the text scan does, so every kind (`option:`, `session:`, `card:`/`#ID`, a path with `:line`,
  an http(s) URL) fills the same `Link` and reaches `Pane::openOutputTarget` by the same road.
- `src/Pane.h` — the open block's URI is handed to the renderer before the chunk is rendered.

## Tasks
- [ ] The fragment convention and the renderer <!-- t:a1 -->
- [ ] The collector and the fold path <!-- t:a2 -->
- [ ] The view: merged anchors and the hit test <!-- t:a3 -->
- [ ] The pane: the anchor per block <!-- t:a4 -->
- [ ] Live drive and evidence <!-- t:a5 -->

## Tests

## QA checklist
