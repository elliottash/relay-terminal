---
id: EVW1
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, theme]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: zzzzj
created: '2026-09-18'
acceptance: 'The filter box and "+ New card" are the first thing on the board''s list page, not in the pane''s header; an open card puts "← Back to board" in that header and nothing else'
source: 'owner, 2026-09-18: "in the board, put ''new card'' at the top of the base page, not in the header of the pane", then "they shouldn''t show when you are clicked on a card. when you are clicked on a card the header should say ''<- back to board''"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-board-list-page-tools/'], related: [T7BQ], github: null}
---
# Board edits: the list's tools sit on the list page, and the header is the way back

## Request
in the board, put "new card" at the top of the base page, not in the header of the pane.

## Change

The pane had one strip of chrome at its very top — the count, the filter box and **+ New card** —
above everything, list or card alike. It is the pane's header: the window's hover buttons float
over its right end. So an open card, which is a page of its own, was wearing the list's controls.

- **The tools moved into the list page.** `BoardView::buildListTools` builds
  `#boardListTools` as the first widget *inside* `#boardListPane`, over the quick-add field and
  the rows. Being inside the list pane, it is on screen exactly when the list is: in a pane
  narrower than 900 px an open card takes the whole pane and the tools go with the list. In a
  wider pane the list stays beside the card and keeps its own tools, because they are the list's
  and not the window's.
- **The header is the way back.** `#boardHead` is hidden whenever no card is open, and holds one
  control when one is: **← Back to board** (`#boardBack`), which does what Esc does. Clicking it
  hints Esc through the existing `onHint` mechanism, per WARP.md's shortcut-hint rule.
- **The hover buttons keep their room either way.** `setHeaderRightInset` used to pad the header
  row alone. It now records the inset and `applyRightInset()` puts it on whichever row is actually
  at the top of the pane — the header while a card is open, the list's tools otherwise.
- **They also make the row too narrow to hold everything in a ~350 px pane**, so
  `layoutListTools()` drops **+ New card** and **Clean up** to a line of their own below the
  filter rather than letting two QToolButtons elide to a pair of identical "…".
- **The format-problems line moved down with them**, from just under the old header to just under
  the checkboxes. Left where it was it would have been the pane's topmost row with the list up,
  and the hover buttons would have covered the file name that fixes the problem.
- `/` (focus the filter) closes an open card first when the card has the pane to itself, rather
  than typing into a box nobody can see.

Filed alongside this: `#T7BQ`, the section checkboxes and the "Clean up" button, which are the
rest of that same strip.

## QA checklist

1. **The list page.** Open the Switchboard (Ctrl+Shift+S). The count, the filter box and
   **+ New card** are the first row *of the list*, with the section checkboxes under them. The
   pane has no separate strip above that.
2. **Open a card** in a narrow pane (drag the splitter under ~900 px, or use a side pane). The
   list and its tools are gone; the top of the pane says **← Back to board** and nothing else.
3. **Click it.** The list comes back, the header goes away, and a hint offers Esc ("Next time:
   Esc") if shortcut hints are on and the hint has not been shown its limit of times.
4. **Esc still works** from the card, and from the reply box (first Esc to the rows, second
   closes), exactly as before.
5. **The pane's hover buttons** (⬓+ ◫+ ⇱ ×) do not sit on top of the filter box, of the problems
   line, or of "← Back to board" at any pane width. Below ~500 px of pane the two buttons take a
   line of their own with their labels whole, never "…".
6. **A wide pane** (> 900 px) with a card open keeps the list and its tools at the left and shows
   the back control at the top. Clicking another row still follows the selection into the card.
7. `/` with a card open in a narrow pane goes back to the board and puts the caret in the filter.

## Known gaps

- The pane's title still reads `Switchboard · 84 open`; the count beside the filter is the one
  that follows the section checkboxes.
- Persisting anything new about the header needs no change; `#T7BQ` names the one `main.cpp`
  line the checkbox state still wants.
