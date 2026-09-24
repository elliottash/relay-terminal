---
id: 0T2R
type: work
status: needs-qa-llm
labels: [bug]
component: [gui]
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context), 2026-09-19
rank: zzzz102
created: '2026-09-18'
acceptance: the always-on pane buttons read as brightly as the hover row used to
source: 'issues/bug_intake.txt, 2026-09-18: "the permanent pane icons should use the brighter outline that we had with the dynamic pane icons"'
links: {plans: [], commits: [4d8af02], evidence: [docs/qa_evidence/2026-09-18-pane-buttons-brighter-outline/], related: [BVK1], github: null}
---
# The permanent pane buttons should keep the brighter outline

## Issue
the permanent pane icons should use the brighter outline that we had with the dynamic pane icons

## Cause

The buttons became permanent, and `1b270ef` ("The pane's buttons stop reacting to the pointer")
then took away everything that had been keyed on hover. What it removed was not only the *changing*
— which is what the owner had asked for — but the look itself:

```
-QFrame#paneChrome[hot="true"] { background: @raised; border-color: @border; }
-QToolButton#paneChromeButton { color: @disabled; ... }
-QFrame#paneChrome[hot="true"] QToolButton#paneChromeButton { color: @muted; }
+QToolButton#paneChromeButton { color: @muted; ... }
```

The glyphs kept the hover row's `@muted` ink, so the ink was never the problem. The **tile** went:
the row has been `background: transparent; border: 1px solid transparent` ever since, so the three
glyphs float on the pane header with nothing around them and nothing saying they are buttons. That
is the "brighter outline that we had with the dynamic pane icons".

The same commit dropped `QFrame#paneChrome[hot="true"]` from the metal and plastic material
stylesheets but left it in the bevel one, where it has been inert ever since — nothing has set the
`hot` property since that commit, so on a Bevel theme the row also lost its moulded edge.

## Change (src/Theme.cpp)

- `QFrame#paneChrome` takes the hover row's own tile, permanently: `background: @raised;
  border: 1px solid @border`. Nothing is keyed on the pointer, so the row is still the same at all
  times, which is what `1b270ef` was for.
- The metal and plastic material stylesheets list `QFrame#paneChrome` among the raised chips that
  take the theme's gradient, so the tile is not the one flat rectangle in a Metal or Plastic window.
- `QToolButton#paneChromeButton:hover` drops `background: @surface`. `@raised` is the top of the
  ground stack, so on the new tile that rule would have made a hovered button **darker** than the
  row it sits on — the old hover row had the same inversion and nobody had a reason to notice. The
  hovered button lifts by ink and a stronger outline instead (`color: @text;
  border-color: @borderStrong`), the way `QPushButton#projectInitButton:hover` in the same
  stylesheet already does.

Nothing outside the stylesheet changed: no widget, no geometry, no property.

## Tasks

- [x] The row keeps the tile and the outline at rest, in every theme
- [x] The metal and plastic face follows
- [x] A hovered button still reads as lifted on the new ground
- [x] Regression test in `tests/themeswitch_test.cpp`, over a dark, a light-square and the fallback theme
- [x] Checked live under Xvfb against a build of the exact tree that lands

## Deliberately left

The bevel material stylesheet still lists the **dead** `QFrame#paneChrome[hot="true"]`: nothing has
set the `hot` property since `1b270ef`, so that rule has never fired and a Bevel theme's row has no
moulded edge. The fix is one word, but it sits inside the same diff hunk as session `activepane`'s
`QWidget#pane[relayActive="true"]` edit, seven lines below, so landing it here would have taken half
of their change with it. Filed as **#BVK1** and landed on its own once that hunk clears. The row
gets the tile and the outline on a Bevel theme either way; only the moulded edge is missing.

## QA checklist
- [ ] Open Relay: each pane's button row sits on a raised, outlined tile, and the three glyphs read as buttons.
- [ ] Nothing changes as the pointer enters or leaves a pane — the row is still the same at all times (this is #1b270ef's own acceptance and must not regress).
- [ ] Hovering one button lifts it: brighter ink and a stronger outline, and it is not darker than the row.
- [ ] A light, square-cornered theme (IBM Beige) shows the tile with the theme's own radius and border colour.
- [ ] A Metal or Plastic theme gives the tile the same gradient face every other raised chip has.
- [ ] `ctest --test-dir build -R theme` and `./scripts/test.sh` pass.
