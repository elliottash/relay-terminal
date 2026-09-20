---
id: 3C7N
type: work
status: needs-verification
labels: [feature, settings]
assignee: agent
implemented_by: kimi/kimi-k3
session: 1aae9fc9-47dd-4eb6-8898-feca80412fc1
rank: zzzzzzzzzzzy
created: '2026-09-20'
source: Relay pane agent, 2026-09-20
links: {plans: [], commits: [260fb350], evidence: [docs/qa_evidence/2026-09-20-tab-theme-marks/], related: [R4ND], github: null}
---
# Tab bar: inactive tab headers wear their own theme

## Issue
can we color the other inactive tabs with their respective themes

(the clickable tab headers)

## What landed
`src/ThemeTabBar.h` (new): the window's tab widget is now a `WindowTabWidget` whose `ThemeTabBar`
repaints the row after `QTabBar` has drawn it. Every inactive tab whose page owns a theme (the
`relayTheme` property — `addTab` pins the default, so every tab owns one) that is *not* the theme
in front is washed in that theme's accent at 16% and carries a 2px strip of it at 85% along its
bottom edge, echoing the selected tab's accent underline (`QTabBar::tab:selected`). The accent is
the only token used: it is the one every theme has that exists to be recognised, and it keeps the
row from becoming a patchwork of theme backgrounds. The front tab wears nothing — the window
around it is already its theme — and with "Each tab keeps its own theme" off the marks are gone
too, because the property is inert then.

| file | change |
|---|---|
| `src/ThemeTabBar.h` | new: `ThemeTabBar` (the marks), `WindowTabWidget` (installs it) |
| `src/RelayWindow.h` | `m_tabs` is a `WindowTabWidget` |
| `tests/themeswitch_test.cpp` | `anInactiveTabWearsItsOwnTheme` (offscreen render, pixel checks) |
| `docs/ARCHITECTURE.md` | the per-tab themes paragraph |

The marks need nothing to trigger them: `chooseTheme()` and `startNewTabTheme()` already called
`tabBar()->update()`, and tab switches repaint the row themselves.

## QA checklist
- [ ] With "Start each new tab on a random theme" (or the cycling mode) on, open three tabs:
      every inactive tab header is tinted a different colour, with a stronger strip of the same
      colour along its bottom edge.
- [ ] The tints are the tabs' themes: bring each tab to the front and the window takes the theme
      its strip suggested; the front tab's mark is gone and the tab you left keeps its own.
- [ ] A tab on the default theme is marked too (its default *is* its theme), in the default's
      accent.
- [ ] Marks survive dragging a tab to a new position, and close with their tab.
- [ ] Options › Appearance › "Each tab keeps its own theme" off: every mark disappears.
- [ ] Hovering a marked tab still reads as a hover (the wash is translucent).
- [ ] `ctest --test-dir build -R themeswitch` passes (17 tests).
