---
id: JDC5
type: work
status: needs-qa-llm
labels: [feature, gui, tabs]
implemented_by: glm/glm-5.3-flash
rank: zzzzzzzzzzi
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {commits: [409e5e11, 7558c9b2, 2eabaa77, fb002856], evidence: [docs/qa_evidence/2026-09-19-tab-header-cleanup/], github: null, plans: [], related: []}
---
# Tab headers lose the theme swatch and the ⧉ detach button

## Issue
remove the thermometer thing and the detach button from tab headers

## What changed
`src/RelayWindow.h`: `paintTabSwatches()` and its Paint-event interception in `eventFilter()`
are gone (the "thermometer" — the per-tab theme swatch at each tab's left edge, its accent
colour filling the bottom 34%), and so is the ⧉ `tabDetachButton` block in
`placeTabBarControls()`. `moveTabToNewWindow` keeps its palette item and tab context menu;
per-tab themes themselves are untouched. `docs/ARCHITECTURE.md` no longer mentions either.
Details and screenshots: `docs/qa_evidence/2026-09-19-tab-header-cleanup/README.md`.
Follow-up (same day): the **attached-project chip** went too — `syncTabProjectChip()`, the
left-box machinery that existed only for it (`tabLeftBox`, `isLiveTabLeftBox`,
`relayoutTabLeftBox`, `m_tabLeftBoxes`), the two `QToolButton#tabProjectChip` rules in
`src/Theme.cpp`, and the `docs/ARCHITECTURE.md` paragraph that promised it. Attachment itself
is untouched (attach/detach, the saved `project` wrapper, `projects::Registry`, the board
tools), and the tab label still names the project (#T7QM). The palette's "Detach this tab
from <project>" is now the only way to detach.

## QA checklist
- [ ] `scripts/relay-build --target relay` succeeds (implementer saw it pass twice, 51 s and 47 s).
- [ ] Under Xvfb with an isolated `XDG_CONFIG_HOME`: two tabs open, pointer on a tab — no
      vertical tube at the left edge of any tab (the swatch's `border_strong` outline leaves
      zero pixels in a scan of the tab row) and no ⧉ between the state icon and the title;
      `docs/qa_evidence/2026-09-19-tab-header-cleanup/after-xvfb-two-tabs-hover.png`.
- [ ] **An attached tab shows no chip**: restore a saved layout whose tab carries
      `{"project": "<repo>"}` with a matching `projects.json` (`before-attached-tab-with-chip.png`
      vs `after-attached-tab-without-chip.png`, `ocr-tab-row-before-after.txt`): the tab row
      reads one project name (the label) where the pre-change build read two (pill + label).
- [ ] Attaching and detaching still work with no chip on screen: opening the Switchboard
      attaches the tab, the palette offers "Detach this tab from <project>", detaching shows
      "This tab is no longer attached to <project>." and the panes lose the card tools.
- [ ] The tab *label* still names the project (#T7QM) and the tab tooltip still lists the pane
      titles, the cwd and the usage line.
- [ ] The close cross still dims on unhovered tabs and keeps its "Close tab"/"Close window"
      tooltip; clicking it still closes the tab (last tab offers close-window).
- [ ] With `theme/per_tab` on and two tabs, `/dark` in one tab still rethemes only that tab,
      switching tabs still switches the theme, and the tab bar paints normally.
- [ ] `tab.moveToNewWindow` still works from the palette and the tab context menu.
- [ ] No reference to `tabProjectChip`, `tabLeftBox` or `syncTabProjectChip` survives
      (`grep -rn` over `src/`, `tests/`, `docs/`).
- [ ] `ctest --test-dir build -j 8`: the only failures are other sessions' `wordwrap` and
      `buttonfit` (their uncommitted `src/WordWrap.*` / `src/Theme.cpp` edits; neither test
      compiles `RelayWindow.h`). `scripts/test.sh`: 3413 backend tests OK.
