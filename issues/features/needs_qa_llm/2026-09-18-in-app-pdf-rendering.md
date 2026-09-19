---
id: WQFS
type: work
status: needs-qa-llm
labels: [feature]
assignee: agent
rank: zzzz11a
created: '2026-09-18'
acceptance: a PDF opens in a Relay pane rather than an external viewer
source: 'issues/feature_intake.txt, 2026-09-18: "add in-app PDF rendering"'
links: {plans: [], commits: [1482551], evidence: [], related: [], github: null}
---
# In-app PDF rendering

## Request
add in-app PDF rendering

## Resolution (found already implemented by the 2026-09-19 board sweep)

A PDF opens in a Relay pane, which is this card's acceptance. `src/FilePanes.cpp` holds a
`QPdfDocument` / `QPdfView` viewer, `FilePane::Kind::Pdf` is one of the pane kinds, and
`showPdf()` / `showRemotePdf()` open a local path and a document fetched from a host (the remote
one reads a `QIODevice`, since there is no local file to read). `CMakeLists.txt` finds
`Qt::Pdf`/`Qt::PdfWidgets` and prints "Relay: PDF preview enabled"; a build without those modules
degrades rather than fails.

Nothing was needed here; the card had simply never been moved.

## QA checklist
- [ ] Ctrl+click a `.pdf` in the folder explorer: it opens in a pane, scrolls and renders.
- [ ] A PDF over a remote session opens too (the `showRemotePdf` path).
- [ ] On a machine without `Qt::Pdf`, the build still configures and a PDF falls back cleanly rather than crashing.
