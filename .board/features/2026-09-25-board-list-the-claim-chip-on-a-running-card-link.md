---
id: YJ4A
type: work
status: executing
labels: [feature, board, panes]
assignee: agent
implemented_by: glm/glm-5.3
session: f9635e9a-6c55-4c92-893a-7133a773ab1f
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: pane 1, 2026-09-25
links: {plans: [], commits: [bbcf3b438801], evidence: [], related: [], github: null}
---
# Board list: the claim chip on a running card links to its pane

## Issue
In the Board cards list, a card claimed by a live pane shows a ⧉ chip that is only paint — the card page's chip is already a relay-pane: link (#R9G7), but from the list there is no way to jump to the pane running the card. Make the list row's claim chip a link that reveals that pane.

> also for running cards, can it show a link to the pane where its running
> — elliott · [session:a29a1c6f0813467f9f3d831c842f6772](relay://session/a29a1c6f0813467f9f3d831c842f6772) · 2026-09-25

## Done means
In the Board cards list, a card row whose claim chip (⧉ xxxxxxxx, #R9G7) names a pane that is still open acts as a link: a click on it reveals that pane (the same `revealClaim` the card page's chip uses), the hover shows the pointing hand, the claimed-by tooltip says so, and the click neither selects the row nor opens the card. A closed pane's chip stays inert paint, and a card nobody claimed shows no chip.

**Verify** (medium): `ctest -R boardpane|boardmodel` on the landed tree — the new `boardpane` case proves the click, the reveal and the closed-pane case by driving `BoardView` with a claimed card and capturing `onFocusPane`.

## Tasks

- [x] Build, targeted tests, land (landed as bbcf3b43 after #HKY4 landed; the restructure hunks of other sessions were left in the tree) <!-- t:0f -->

## Execution Summary
All code is in the working tree and verified there: `RowDelegate::sessionChipRectOf` (the placed Session badge's rect, empty for a closed pane or a dropped badge), the `RowList` click guards + pointing-hand hover + `onRevealPane`, the `buildChrome` wiring into `revealClaim`, the claimed-by tooltip line, `BoardView::claimChipRect`, and the test `aClaimedCardsRowLinksItsChipToThePane` (tests/boardmodel_test.cpp). `ctest -R '^board$|^boardsections$'` passes on the shared build; the failing `boardpane` case is #HKY4's own uncommitted test, not this change.

Landing is blocked behind a whole-file merge: #HKY4's and #C52H's uncommitted `src/BoardPane.cpp` hunks (they began before my snapshot and kept editing after it) conflict textually with tip from my snapshot's side, and my `onRevealPane` member sits at #HKY4's `onOpenOwnPane` insertion anchor. Hand-merging would overwrite their uncommitted work, so this session waits for either to land and then re-runs `land.py commit dropfix` — the three-way merge takes both sides' insertions cleanly once their lines are on tip. Both panes were pinged (p43 = #C52H; #HKY4 via its card).

## Tests
`ctest -R '^board$'` on a clean export of the landed commit `bbcf3b43` (git archive into a scratch dir, fresh configure, `relay-board-tests` target): 100% passed, including the new `BoardModelTests::aClaimedCardsRowLinksItsChipToThePane` — the chip click reveals the claiming pane's token through `onFocusPane`, opens no card, sends nothing; an unclaimed card has no chip; a closed pane's chip is inert. `boardsections` passed on the shared build and in the `land.py try` slot run. Full record: `docs/qa_evidence/2026-09-25-claim-chip-link/evidence.md`. The one failing `boardpane` case during development was #HKY4's own uncommitted test, untouched by this change.
