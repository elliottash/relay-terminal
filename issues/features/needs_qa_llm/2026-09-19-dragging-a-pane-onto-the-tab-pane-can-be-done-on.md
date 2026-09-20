---
id: A0SF
type: work
status: needs-qa-llm
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzi
created: '2026-09-19'
links: {commits: [81ff60f8, 15aafa59], evidence: [docs/qa_evidence/2026-09-20-drag-pane-onto-tab-label/], github: null, plans: [], related: []}
---
# dragging a pane onto the tab pane can be done on an existing tab or a new tab

## Issue
dragging a pane onto the tab pane can be done on an existing tab or a new tab. right now it always creates a new tab. but if you drag onto another tab header mark, it moves the pane into that other tab.

## Plan
**Goal.** Dragging a pane by its header (or a tool pane's ⠿ grip) onto the tab bar drops it into an *existing* tab when the cursor is over that tab's label, and only creates a new tab when the cursor is over empty tab-bar space. Today every tab-bar drop creates a new tab.

**Findings.** All of the work is in `src/RelayWindow.h`:
- `enum class Edge { None, Left, Right, Top, Bottom, TabBar }` (line 6363) and `static QPair<QWidget *, Edge> dropTarget(QWidget *dragged, const QPoint &global)` (6366): when the widget under the cursor is a `QTabBar` it returns `{bar, Edge::TabBar}` with no idea *which* tab (or none) is under the cursor.
- `dragPaneMove` (6453) draws the `m_dropZone` highlight rect; for `Edge::TabBar` there is no per-tab feedback (the Left/Right/Top/Bottom cases halve the rect at 6466–6469, TabBar falls through).
- `dragPaneEnd` (6476) is where the drop lands: the `Edge::TabBar` branch (6499–6504) only guards "already its own tab here" (`w == this && leavesIn(pageOf(dragged)).size() <= 1`) and then always does `takeLeaf(dragged)` + `w->adoptLeafAsTab(dragged)` — a new tab, every time. This is the one place that must change.
- Reusable pieces already exist: `takeLeaf` (6534) detaches the pane and removes the source tab if it became empty (`m_tabs->removeTab` at 6548); `insertBeside(anchor, pane, orientation, before)` (5466) docks a leaf beside any anchor leaf; `leavesIn(page)` lists a page's leaves; `QTabBar::tabAt()` maps a point to a tab index (already used at 778 and 6004); `w->m_tabs->widget(index)` gives the tab's page. Cross-window drops are already routed (`w = windowOf(target.first)`), and the source-window `takeLeaf` / target-window insert split used by the TabBar branch is the pattern to copy.

**Steps.**
1. In `dropTarget` (or just in `dragPaneEnd`/`dragPaneMove`, reading `target.first`), resolve the tab index under the cursor: `const int tab = bar->tabAt(bar->mapFromGlobal(global));` `-1` means empty tab-bar space → keep today's new-tab behavior.
2. In `dragPaneEnd`'s `Edge::TabBar` branch: when `tab >= 0`, let `page = w->m_tabs->widget(tab)`. No-op if `page == pageOf(dragged)` (dropped on the tab it already lives in). Otherwise `takeLeaf(dragged)` (source window, as today), then dock it into that page: `w->insertBeside(w->leavesIn(page).first(), dragged, Qt::Horizontal, false)`, then `w->m_tabs->setCurrentWidget(page)`, `w->setActiveLeaf(dragged)`, `focusLeaf(dragged)`, and the existing `w->raise()/activateWindow()` for the cross-window case. Confirm `insertBeside` behaves on a non-current page — it only manipulates the splitter tree, but if it assumes visibility, briefly make `page` current first.
3. In `dragPaneMove`, give per-tab feedback: when the target is `Edge::TabBar` and `tab >= 0`, size `m_dropZone` to `bar->tabRect(tab)` (mapped into drop-zone coordinates) instead of the whole bar, so the user can see "into this tab" vs "new tab" before releasing.
4. Update the in-app help text at `src/RelayWindow.h:2715` ("…or onto the tab bar to give it a tab of its own") to say a drop on a tab's label joins that tab, a drop on empty tab-bar space makes a new tab.

**Risks.**
- `insertBeside` on a background (non-current) page is the one unverified assumption; step 2 says how to fall back.
- Placement inside the target tab: the plan docks the pane as a horizontal split beside the tab's first leaf (right edge of its content). If the owner wants it somewhere else (e.g. remember the edge over the tab label), say so before executing.
- `m_tabs->setMovable(true)` tab-reordering drags are a different gesture (they start on the tab itself), so no conflict — but check the new-tab button (`m_newTabButton`) hover: a drop over it should stay "new tab", matching `tabAt() == -1`.
- Dropping the only pane of a tab onto another tab closes the now-empty source tab via `takeLeaf` — same as today's new-tab path, but confirm no closed-tab record regression (that path does not record to the closed stack today either).

**Verify.**
- Build with `scripts/relay-build`; land with `scripts/land.py` per WARP.md.
- No existing unit tests cover pane drag (only `relay::board::dropTarget` in `tests/boardmodel_test.cpp`, unrelated). If a cheap harness exists for `RelayWindow` splitter moves use it; otherwise verify live under Xvfb with an isolated `XDG_CONFIG_HOME`: (a) two tabs, drag a pane onto the other tab's label → pane joins that tab as a split, old tab closes if it emptied, shell/agent session survives; (b) drag onto empty tab-bar space → new tab, as today; (c) drag onto the pane's own tab label → nothing happens; (d) drop over the "+" new-tab button → new tab; (e) the `m_dropZone` highlight tracks the hovered tab label.

## QA checklist
Live checks (no automated coverage; Xvfb pass skipped at the owner's request, 2026-09-20 22:55):

- [ ] Two tabs: drag a pane's header from one tab onto the *other* tab's label → the pane docks into that tab beside its first leaf, the emptied source tab closes, and the moved pane's shell/agent session still runs.
- [ ] Drag onto empty tab-bar space → a new tab, as before.
- [ ] Drop over the "+" new-tab button → a new tab (not an existing-tab join).
- [ ] Drop on the pane's own tab label → nothing happens.
- [ ] Mid-drag, the drop highlight hugs the hovered tab label and covers the whole bar over empty space; Esc during the drag puts the pane back.
- [ ] Cross-window: drop on another window's tab label joins that tab and raises the window.
- [ ] Options → mouse help and the pane header tooltip read correctly for both drops.
