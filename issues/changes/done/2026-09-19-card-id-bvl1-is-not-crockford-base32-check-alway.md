---
id: 1KXX
type: work
status: done
labels: [bug, switchboard]
rank: zzzzzzzzzzzzzzr
created: '2026-09-19'
links: {plans: [], commits: [02d65e5f], evidence: [], related: [BVK1], github: null}
---
# card id BVL1 is not Crockford base32; check always reports an error

## Issue
Found while migrating this board for #3XZV: `scripts/relay-board.py check` reports `bad_id: id 'BVL1' is not 4 Crockford-base32 characters with a letter` on issues/changes/2026-09-19-the-bevel-stylesheet-still-keys-the-pane-row-on.md. The id uses L, which Crockford base32 excludes, so every check run reports an error. The id is referenced by at least two other cards (issues/changes/needs_qa_llm/2026-09-18-pane-buttons-brighter-outline.md task marker card=BVL1, and issues/BOARD.md), so fixing it means renaming the id everywhere it is referenced, not just the front matter. Pre-existing on HEAD; not caused by the stage migration.

## Fix
The card is now **#BVK1** — K for the excluded L, so the id still reads as the bevel card and the
old one stays recognisable in history. Renamed in the three places the board reads:

- `issues/changes/2026-09-19-the-bevel-stylesheet-still-keys-the-pane-row-on.md` — the `id:` field.
- `issues/changes/needs_qa_llm/2026-09-18-pane-buttons-brighter-outline.md` — `links.related` and
  the `**#BVK1**` reference in its body.
- `issues/BOARD.md` — that card's row, edited in place rather than by `relay-board.py index`, so the
  regenerated index could not sweep in the card edits other sessions have uncommitted in the tree.

`docs/qa_evidence/2026-09-18-pane-buttons-brighter-outline/README.md`,
`docs/qa_evidence/2026-09-20-switchboard-sections/README.md` and `issues/threads/3XZV.md` still say
BVL1 on purpose: they are records of what was true when they were written.

## Verification
`python3 scripts/relay-board.py check` no longer reports `bad_id` — 306 cards, 0 errors. The one
remaining warning (`task_missing_marker` on the pane-buttons card) is pre-existing and is not this
card's.
