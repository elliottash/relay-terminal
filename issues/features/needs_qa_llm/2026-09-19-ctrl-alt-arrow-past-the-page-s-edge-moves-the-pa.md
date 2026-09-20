---
id: 8G7E
type: work
status: needs-qa-llm
labels: [feature, gui, keyboard]
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzi
created: '2026-09-19'
source: recovered from the stopped "Moving panes past page edges" agent session (81abd1b4), 2026-09-19; landed by the continue session
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-pane-move-past-page-edge/], related: [JXWT], github: null}
---
# Ctrl+Alt+arrow past the page's edge moves the pane into a column or row of its own

## Issue
for stacked panes at the right edge of the screen, if you press ctrl alt right it says "no pane in that diretiction". but instead, it should move the pane to the right in its own column. plan out this fix and the equivalent behavior for other screen edges

## QA checklist
- [ ] Stack of panes in the rightmost column: Ctrl+Alt+Right on the bottom pane gives it a new rightmost column of its own; the stack keeps the rest; shell, agent and scrollback travel with it; no terminal goes blank.
- [ ] Ctrl+Alt+Right again on that now-lone rightmost pane: the notice "This pane already has that edge to itself.", and no re-layout.
- [ ] The other edges mirror it: Ctrl+Alt+Left out of a stack in the leftmost column (new leftmost column), Ctrl+Alt+Up from a two-pane top row (new full-width top row), Ctrl+Alt+Down from a two-pane bottom row (new full-width bottom row).
- [ ] A page that is one stack of two (rows): Ctrl+Alt+Right wraps the root and the pane becomes a full-height right column — and both panes are still there (the two-pane case where takeLeaf unwraps the root).
- [ ] One pane in the tab: the move says "This pane is already the only pane in its tab."
- [ ] Existing panes keep their relative sizes; the newcomer takes one equal share of the page's top-level regions.
- [ ] The moved layout survives quit and relaunch.
- [ ] `ctest --test-dir build -R "^panes$"` green (new slots `fillingTheEdgeMeansNothingToMove`, `edgeDockGivesTheNewcomerOneEqualShare`).
- [ ] Live evidence in `docs/qa_evidence/2026-09-19-pane-move-past-page-edge/`: `drive.sh` (Xvfb + xdotool), per-move `stty size` geometry reports, 13 screenshots including both notices, restore-after-relaunch.
