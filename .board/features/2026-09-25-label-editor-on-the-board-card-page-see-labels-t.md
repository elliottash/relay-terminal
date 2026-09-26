---
id: E0Y0
type: work
status: needs-verification
labels: [feature, board]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: f35e3cfe-5e98-4de5-a71d-2825d5bd3a56
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'An × click rewrites the card''s `labels:` front matter (normalized file, thread change line) and the meta line re-renders; the + field adds a label; Esc writes nothing. ctest -R boardpane, label editor cases pass.', sign_off: none, effort: low, stakes: nuisance}
source: pane f35e3cfe, 2026-09-29
links: {plans: [], commits: [dd02b23a4bbd, ccc281880ba9], evidence: [dd02b23a + ctest -R boardpane (pass), docs/qa_evidence/2026-09-25-e0y0-card-page-label-editor/], related: [], github: null}
---
# Label editor on the Board card page (see labels, × them out)

## Issue
The Board card page shows labels read-only in the meta line (each label already a `tag:` link). Add an editor there: each label gets an × to remove it, and a way to add a new label. Fields are written through the worker's card-write path so front matter stays normalized.

> i want to add a label editor in the card page. you can see the labels and x them out
> — elliott · [session:9da0de82fc0b4724bc5f5bf81c06d541](relay://session/9da0de82fc0b4724bc5f5bf81c06d541) · 2026-09-25

## Done means
On the Board card page (`CardDetail`, `src/BoardPane.cpp`):

- every label in the meta line is followed by an **×**; clicking it removes that label from the card's front matter through the same hash-checked `board_update` message the title save uses, and the meta line re-renders without it.
- the labels row carries a **+** link; clicking it opens a one-line field pre-filled with the card's labels, comma separated. Enter saves the whole list (adds, removes, reorder), Esc closes without writing.
- a card with no labels still shows the labels row (just **+**), so the first label can be added from the empty state.
- the label word itself still copies its board-filter term (#3ZAP); only × and + edit.

Tests in `tests/boardpane_test.cpp`: × removes a label (front matter written, meta re-rendered), the + field adds one, Esc writes nothing, and the plain label link still filters/copies.
