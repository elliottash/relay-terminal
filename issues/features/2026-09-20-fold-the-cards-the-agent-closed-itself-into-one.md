---
id: 93WR
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
rank: i1
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [9a82b1af, d5128e04, dedad97c, a8fe410c, 77aaf5e1], evidence: [docs/qa_evidence/2026-09-20-self-closed-fold], related: [R9G7, 7BM4], github: null}
---
# Fold the cards the agent closed itself into one row of the done list

## Issue
ok, coming back to the medium fold vs signals, i see the value of that.

write cards on both. medium folding seems like a no brainer. so implement it with opus.

## Decisions
- The fold is presentation, not a type (2026-09-20): a medium card is ordinary work that the agent closed itself, so it stays a work card in the done lane and the board folds it.
- Marker, no new field: when the agent closes its own card to `done` from a non-QA status, Relay stamps `verified_by` with the closing pane's provider/model, as it already does for a QA close. A self-closed card is one whose `verified_by` equals its `implemented_by`.
- Wherever done cards are listed, self-closed cards collapse into one row, "N closed by the agent", folded by default, unfolded by a toggle and by the same keys a section uses; a filter or a search that matches one shows it.

## Tasks

- [x] Backend: stamp `verified_by` on an agent's own close from executing/in-progress; rows carry it; tests; format and protocol docs
- [x] GUI: the folded row in the done lists, toggle, keys, saved with the layout, search shows matches; tests; evidence

## QA checklist

- [ ] In a pane, have the agent do a medium piece of work and close its own card: the card's front matter has `verified_by` equal to `implemented_by`, and the move's thread line says `· verified_by <model>`.
- [ ] Close a card by hand from the Switchboard: no `verified_by` is stamped and the card stays an ordinary row.
- [ ] The Done list shows `▸ N closed by the agent` after its ordinary cards; the section header's count includes the folded cards.
- [ ] Click, Enter and → show them; ← puts them away and stands on the row; ← again folds the section.
- [ ] Open, Execute, move, delete and drag do nothing on the fold row.
- [ ] Type a filter that matches a folded card: it appears as an ordinary row and the fold row counts the rest.
- [ ] Jump to a folded card by `#ID`: its group unfolds and the card is selected.
- [ ] Unfold, restart Relay: the group is still unfolded; a layout saved before this change restores as before.
- [ ] A self-closed card sits in Done, not Verified, and wears no ✓ badge.
