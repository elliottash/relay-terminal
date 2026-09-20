---
id: BXCN
type: work
status: needs-verification
assignee: agent
implemented_by: kimi/kimi-k3
priority: 1
rank: zzzzzzzzzzzzzi
created: '2026-09-19'
links: {plans: [], commits: [f4beffd3bcba026d96a6d0b951d0fc3dafe49b09, 83a94001e2b6e6dbd5f2f6d76f6b9dcfd82d3f1b], evidence: [docs/qa_evidence/2026-09-20-board-split-floor/], related: [], github: null}
---
# when you do the "auto-organize panes", make the switchboard wide enough to allow…

## Issue
when you do the "auto-organize panes", prioritize makeing the switchboard wide enough to allow the separate card split 

also, when you "execute" from the switchboard, protect the switchboard size so that it wont shrink further than the minimum space needed for the list-card split. shrink the other panes instead.

## Plan
**Goal.** Two flows must keep the Switchboard pane wide enough for its list/card split: "Equalize
pane sizes" (`pane.equalize`, Ctrl+Alt+0) gives the board pane its split width before dividing the
rest equally, and Execute (`x`) / Verify (`v`) never take the new terminal pane's width out of the
board pane below that width — the other panes in the splitter shrink instead.

**Findings.**

- The split threshold is `kStackedWidth = 900` (`src/BoardPane.cpp:80-81`, anonymous namespace):
  `BoardView::resizeEvent` (`src/BoardPane.cpp:4382-4432`) hides `m_listPane` and stacks the card
  over the list whenever the pane is narrower. So "minimum space needed for the list-card split"
  is 900 px (the hard layout minimum is only 240 + 320 + handle ≈ 576, which Qt already enforces —
  the pane can be legally squeezed to where the split collapses).
- `equalizeActivePage` (`src/RelayWindow.h:6796-6812`) sets every splitter in the tab to equal
  shares (`equal.append(1000)` per entry, `setSizes`). With 3+ panes the board's equal share is
  routinely under 900, so equalize stacks it.
- Execute (`RelayWindow.h:4702-4713`) and Verify (`RelayWindow.h:4729-4740`) create a terminal
  pane and `insertBeside(guard /* the board pane */, pane, Qt::Horizontal, false)`.
  `insertBeside` (`RelayWindow.h:5493-5530`) docks it with `relay::panes::sizesAfterDock`
  (`src/PaneLayout.h:170`, `src/PaneLayout.cpp:244-260`), which splits **only the anchor's share**
  in half — the new pane's whole width comes out of the Switchboard, halving it below 900 on the
  first Execute and further on later ones.
- The board pane is a `ToolPane` leaf with `kind() == ToolPane::Kind::Board`
  (`src/PaneChrome.h:388`, ctor at `:421`).

**Steps.**

1. Hoist `kStackedWidth` out of the anonymous namespace into `src/BoardPane.h` as
   `relay::board::kCardSplitWidth` (same 900, keep its comment); `BoardView::resizeEvent` uses it.
   Add a tiny helper in `RelayWindow.h`, `boardSplitFloor(QWidget *leaf)`, returning
   `relay::board::kCardSplitWidth` when the leaf is a `ToolPane` of `Kind::Board`, else 0.
2. `src/PaneLayout.{h,cpp}`: extend `sizesAfterDock` with a trailing `int anchorFloor = 0`
   (every existing caller and test is unchanged), and add
   `QList<int> sizesAfterEqualize(const QList<int> &sizes, const QList<int> &floors)` — both pure,
   same style as `sizesAfterEdgeDock`:
   - `sizesAfterDock`: the newcomer still takes half the anchor's share, but never more than
     `max(0, share - anchorFloor)`; any shortfall comes proportionally from the other entries, so
     the anchor stays at or above its floor and the list total is preserved. Keep today's guards
     (empty sizes, bad index, zero share → empty).
   - `sizesAfterEqualize`: equal shares, except entries whose floor exceeds the equal share are
     pinned at the floor and the remainder is split equally among the rest. If the floors cannot
     be afforded (their sum eats the total), return plain equal shares — today's behaviour.
3. `equalizeActivePage`: for each splitter in the page, build `floors` from
   `boardSplitFloor(splitter->widget(i))` (0 for every non-board pane; for vertical splitters use
   all-zero floors — this floor is a width), then `setSizes(sizesAfterEqualize(splitter->sizes(),
   floors))`. If the splitter's sizes sum to 0 (not laid out yet), keep the 1000-per-entry ratio
   path. Notice text unchanged.
4. `insertBeside`: add a trailing `int anchorFloor = 0` parameter and pass it to `sizesAfterDock`;
   in the wrap path (anchor alone, new splitter) apply the same floor to the queued 50/50 guard —
   the anchor gets `max(total / 2, min(anchorFloor, total - newcomer's minimumSizeHint().width()))`.
   Execute and Verify pass `boardSplitFloor(guard)`; all ~30 other `insertBeside` callers keep the
   default and behave exactly as today.

**Risks.**

- "Equalize" is no longer strictly equal when a Switchboard pane is in the tab — that is the
  request, but the other panes come out smaller than equal shares; the wording of the action
  ("every splitter back to equal shares") stays acceptable since it is still the tidy-layout path.
- A tab too narrow for 900 px plus the other panes' minimums falls back to today's behaviour in
  both flows; Qt still clamps at each pane's minimumSizeHint, so nothing can go negative or clip.
- Two Switchboard panes in one tab both want 900; `sizesAfterEqualize`'s affordability fallback
  handles it (equal shares when the tab cannot pay for both).
- Deliberately **not** raising `BoardView`'s own `minimumSizeHint`: a hand drag can still narrow
  the board. The card asks for priority in organize and Execute, not a hard pane minimum.

**Verify.**
- Unit tests in `tests/panelayout_test.cpp`: extend the `sizesAfterDock` table (:234-256) with
  floor cases (floor affordable / not affordable / anchor already below it) and add a
  `sizesAfterEqualize` block next to the equalize test at :359 (floor pinned and others equal;
  unaffordable floor → plain equal; all-zero floors → exactly today's output).
- Build with `scripts/relay-build`; run `ctest --test-dir build -R panelayout` (targeted, per the
  repo test rule) plus `boardmodel_test` if BoardPane.h moved anything it uses.
- Live check under Xvfb with an isolated `XDG_CONFIG_HOME`: open the Switchboard, open a card (the
  list/card split visible), split two more terminal panes, Ctrl+Alt+0 → the board pane stays
  ≥ 900 px and the split survives while the terminals share the rest; press `x` (Execute) on a
  card → the new terminal takes its width from the *other* panes, the board keeps the split;
  Execute again → still ≥ 900. Land through `python3 scripts/land.py begin` / `commit`, with
  evidence under `docs/qa_evidence/2026-09-20-board-split-floor/` and a QA checklist on the card.

## QA checklist
- [ ] `ctest --test-dir build -R '^panes$'` — 45/45 pass, including `dockingKeepsTheAnchorAboveItsFloor` and `equalizingGivesTheBoardPaneItsSplit` (the pure floor arithmetic both flows share).
- [ ] Live: open the Switchboard with a card open in a wide tab, Ctrl+Alt+0 — the board keeps its list beside its card while the other panes share the rest (saved-layout ground truth: board lifted to its equal share ≥ 902; pixel drive `06 equalize … yes`).
- [ ] Live: press `x` on the card — the new terminal's width comes out of the panes beside the board, the board keeps ≥ 902 and the split (`ground-truth-minimal.txt`: `[594, 901, 450]`, newcomer 450 ≈ its half, board 901 at the floor).
- [ ] Live: a second `x` in a tab that can no longer afford 902 + the terminals' minimums clamps the board (Qt's minimumSizeHint) — the documented fallback, not a regression.
- [ ] A hand drag can still narrow the board below 900 (deliberate: the floor is what organize/Execute ask for, not a hard minimum).
- [ ] Follow-up when the board file's other in-flight edits land: switch `BoardView::updateDetailLayout`'s `kStackedWidth` to `board::kCardSplitWidth` (the two one-line hunks left uncommitted; the duplicate 900 is harmless meanwhile).
- Note for the verifier: `tests/boardmodel_test.cpp`'s `theDeleteKeyAndButtonDeleteTheCardAndTheUndoToastSurvives` currently hangs — another session's mid-flight delete-card work in this shared checkout, unrelated to this card (28/29 board tests pass).
