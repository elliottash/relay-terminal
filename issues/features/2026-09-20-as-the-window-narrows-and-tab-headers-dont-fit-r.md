---
id: VWSD
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzi
created: '2026-09-20'
links: {plans: [], commits: [ab3b96c3, 03e11b91], evidence: [docs/qa_evidence/2026-09-20-tab-meter-give-way/], related: [], github: null}
---
# as the window narrows and tab headers dont fit, remove the cpu / memory indicato…

## Issue
as the window narrows and tab headers dont fit, remove the cpu / memory indicators

## Plan
**Goal.** When the tab bar no longer has room for every tab's full label, the tab labels drop their
`  ·  cpu NN% · mem NN%` suffix so the tab *names* get the width back, and the suffix returns when
the bar is wide enough again. The reading itself is not lost: it stays in the tab's tooltip. Nothing
else about the meters changes (the pane-header chip, the Sessions tag, the setting).

**Findings.**

- The tab label is built in `RelayWindow::tabLabelText()` — `src/RelayWindow.h:6540`:
  `tabLabelFor(page, titles)` (5418, the place name) `+ tabUsageSuffix(page)` (6545, the meter,
  from `tabUsageSuffix()` at 6499, which reads the cached text or `relay::usage::tabSuffix()` at
  `src/PaneUsage.cpp:416`) — and is then elided at a **fixed** 260 px (6546,
  `metrics.elidedText(title, Qt::ElideRight, 260)`). The label is never told how wide the bar is,
  which is the whole of the card.
- Labels are (re)written in two places: `updateTitles()` (5490-5498, every open/close/split/rename)
  and the tab's own 5 s clock inside `refreshPaneStatus()` (6311; 6373-6397, which relabels only a
  tab whose text actually changed — `previousText` at 6376).
- The bar is `m_tabs->tabBar()`: `setExpanding(false)` (368), `setMovable(true)` (367),
  `setDocumentMode(true)` (365). It sets **neither** `setElideMode` nor `setUsesScrollButtons`, so
  Qt's defaults apply; the theme's padding is `QTabBar::tab { padding: 5px 8px; margin: 3px 1px 0 1px }`
  (`src/Theme.cpp:610`). The window chrome sits in the bar's corner widgets —
  `setCornerWidget(left, Qt::TopLeftCorner)` (5878, `windowChromeLeft` 5854) and right (5945,
  `windowChromeRight` 5881) — so they take width out of the tab strip inside `bar->width()`.
- The bar already has an event filter (`bar->installEventFilter(this)` 5820) and its `Resize` case
  (835-837) runs `placeTabBarControls()` (6196, which parks the `+` button after the last tab and
  clamps it to `bar->width()`), through a 0-timer. That is the hook for a width-driven relabel.
- Precedent for the shape of this rule: `relay::panes::headerFit()` in `src/PaneLayout.{h,cpp}`
  (declaration `PaneLayout.h:127`) — a pure function of the width, unit-tested in
  `tests/panelayout_test.cpp`; `docs/ARCHITECTURE.md` ~126-151 and ~393-470 record that ladder and
  the meters (the sentence at 414, "the meter is the last thing in the header to give way").

**Steps.**

1. `src/PaneUsage.h/.cpp`: add the rule next to `tabSuffix()` —
   `bool tabMetersFit(int barWidth, const QList<int> &fullLabelWidths)` returning
   `Σ fullLabelWidths <= barWidth` (an empty list, and a bar with no tabs, are `true`). No QWidget,
   so `tests/paneusage_test.cpp` can drive it. Document it there as the tab bar's rung: the suffix
   is the first thing the tab strip gives up.
2. `src/RelayWindow.h`, near `tabUsageSuffix()` (~6499): `int tabsFullWidth() const` — per tab,
   `bar->tabSizeHint(i).width()` plus the suffix's own advance (`QFontMetrics(bar->font())`,
   `relay::usage::tabSuffix(sample)`) **when the suffix is not currently in `bar->tabText(i)`**.
   Both states then yield the same full width, so the answer does not depend on what is drawn
   (no feedback).
3. Same place: `bool tabMetersHaveRoom() const` = `relay::usage::tabMetersFit(usable, tabsFullWidth())`,
   with `usable = bar->width() - leftCorner->width() - rightCorner->width()` (the corner widgets of
   finding 3).
4. `tabLabelText()` (6540): append `tabUsageSuffix(page)` only when `tabMetersHaveRoom()`; keep the
   260 px elide and `tabLabelFor()` as they are. `updateTitles()` then needs no change — every tab
   it writes already goes through here, so a new, closed or moved tab is right by construction.
5. The width-driven relabel: in the event filter's tab-bar **`Resize`** case only (835, not the
   MouseMove/Leave branch), schedule `relabelTabsForWidth()` — a loop over the tabs calling
   `m_tabs->setTabText(i, tabLabelText(page, titles))` behind the same "text actually changed"
   guard the 5 s clock uses (6376), so dragging a window edge costs nothing until the answer flips.
6. The 5 s clock path (`refreshPaneStatus()`, 6373-6397): keep feeding `m_tabUsage[page]`'s rolling
   window and rewriting `state.text`, but write the label without the suffix while
   `!tabMetersHaveRoom()`, so the suffix comes back with a current reading rather than a stale one.
   `tabTooltipText()` (6513) is untouched: the reading stays on hover, which is what makes dropping
   it from a narrow label cheap.
7. Docs, in the same commit: `docs/ARCHITECTURE.md` §3's give-way ladder (~126-151) and the
   resource-meter section (~393-470, including the 414 and 440/456 sentences) gain the tab-bar rule
   — the suffix is dropped when the bar is out of room, the decision is a pure function of the bar's
   width and the labels' full widths, and the reading lives on in the tooltip; `src/PaneUsage.h`'s
   tab-label paragraph (the comment above `tabSuffix`) gains a sentence.

**Risks.**

- **Oscillation** is the one way this can look broken: if the rule is asked from the *shown* labels,
  dropping the suffix makes the tabs fit and the suffix would come straight back. Step 2's
  reconstruction of the full width is what prevents it; the flip must be a function of the bar's
  width alone.
- **Where exactly the threshold lands** (corner widgets, the QSS padding, and whether Qt is
  eliding the labels today or showing scroll buttons) has to be seen, not reasoned: the evidence
  must carry a shot either side of the flip and a note saying which behaviour the bar has today.
- **The tab count changing** changes the sum — steps 4 and 5 cover opening, closing and moving a
  tab, but the close path (`requestCloseTab`) and `moveTabToNewWindow` are worth a look while
  testing.
- **Question for the owner (not a blocker):** the suffix drops *whole* when the room goes, as the
  card asks, or should the memory half go first as a middle rung, mirroring the pane header's rung 4
  (`cpu 12%`)? My recommendation is whole: the two-digit suffix is a fixed width on *every* tab, and
  keeping half of it would still spend the names' room on a half that reads `mem 00%` most of the
  time. I will build it whole unless you say otherwise.
- This is a tab-bar rule only. The pane header keeps its own ladder (rung 4 = `cpu` alone), and
  `relay::usage::metersEnabled()` (`appearance/pane_usage`) is untouched: turning the meters off is
  still the way to lose them everywhere.

**Verify.**

- `scripts/relay-build`, then `ctest --test-dir build -R paneusage` with new `tabMetersFit` cases:
  exactly fitting, one pixel over, an empty list, a zero-width bar.
- A source-text test in the `tests/boardworkspace_test.cpp` idiom (`bodyOf(windowSource(), "…")`,
  RELAY_SOURCE_DIR): `tabLabelText()` gates `tabUsageSuffix` on the fit rule, and the clock path
  (6373-6397) never writes the suffix when there is no room. Home it in `tests/panetitles_test.cpp`
  ("Pane titles and tab labels") if that target has RELAY_SOURCE_DIR, else beside the existing
  RelayWindow.h text tests.
- Live, under Xvfb with an isolated `HOME`/`XDG_CONFIG_HOME` (WARP.md), in a
  `docs/qa_evidence/2026-09-20-tab-meter-give-way/` folder with a `drive.sh`: four or five tabs with
  long names, cropped tab strip at 900 → 760 → 640 → 520 → 420 — suffix present while the tabs fit,
  gone at the first width where they do not, names whole (not elided) once it is gone — then widened
  back with the suffix returning; plus two takes ~6 s apart at a narrow width under load showing the
  label steady (no flip-flop). Land with the QA checklist and the evidence path, per WARP.md.

## QA checklist
- [ ] `relay::usage::tabMetersFit()` unit cases pass: exactly fitting, one pixel over, empty list, zero-width bar (`ctest --test-dir build -R paneusage`) — implementer: passed.
- [ ] Source-text tests pass: `tabLabelText()` gates `tabUsageSuffix` on the fit rule, and the 5 s clock path never writes the suffix when there is no room (`tests/boardworkspace_test.cpp`, paneusage/titles targets) — implementer: passed.
- [ ] Evidence `docs/qa_evidence/2026-09-20-tab-meter-give-way/` shows, on the cropped tab strip: suffix present at 900/760 while tabs fit, gone at the first width where they do not (640/520/420), tab names whole (not elided) once the suffix is gone, and the suffix returning when the bar is widened again.
- [ ] Two takes ~6 s apart at a narrow width (420) show the label steady — no oscillation/flip-flop across the 5 s clock.
- [ ] The reading still appears in the tab tooltip at narrow widths.
- [ ] Note in the evidence which behaviour the bar has at the threshold today (label elide vs scroll buttons), per the plan's risk.
- [ ] Pane header chip, Sessions tag and `appearance/pane_usage` setting unchanged.
