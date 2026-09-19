# The permanent pane buttons keep the brighter outline (#0T2R, 2026-09-19)

Implementer evidence. Owner's report: *"the permanent pane icons should use the brighter outline
that we had with the dynamic pane icons"* (`issues/bug_intake.txt`, 2026-09-18).

`1b270ef` made the pane's button row permanent and removed everything keyed on hover — including
the tile that the hover row had carried (`background: @raised; border-color: @border`). The glyphs
kept the hover row's `@muted` ink, so what the owner is missing is the tile and its outline, not the
ink. This change gives the permanent row that tile, permanently.

## How the shots were taken

`drive.sh <build-dir> <prefix>`, run twice against the **same** scratch build directory
(`/tmp/outline-qa/build`, Qt 6.4.2), rebuilt in between with only `src/Theme.cpp` swapped:

- `implementer-before-*` — that tree at its base version of `Theme.cpp`.
- `implementer-after-*` — the same tree with this change's hunks, i.e. **exactly what lands**.

Not the shared `build/`: at the time of the run another session had `src/Pane.h` mid-edit and the
shared tree did not compile, and the shared `Theme.cpp` also carried a third session's
board-material work. The scratch tree is `git archive` of the tip current at the time plus this
change's three hunks and nothing else.

Xvfb `:91`, 1400×820, an isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`XDG_CACHE_HOME` and a
throwaway workspace, `RELAY_KEYRING=off`. The repository's own tree was never opened.

| Shot | What it shows |
|---|---|
| `implementer-{before,after}-01-one-pane-dark.png` | One pane, Dark Copper. |
| `implementer-{before,after}-crop-01-one-pane-dark.png` | The row at 4×. **Before:** four glyphs floating on the header, nothing around them. **After:** the raised copper tile with its outline. |
| `implementer-{before,after}-03-two-panes-pointer-away.png` | Two panes, pointer well away from both rows. |
| `implementer-{before,after}-04-two-panes-pointer-in-left-pane.png` | The same, pointer inside the left pane's body. |
| `implementer-{before,after}-crop-05-pointer-on-the-close-button.png` | A hovered button at 4×: it lifts by ink and a stronger outline, not by a ground. |
| `implementer-{before,after}-crop-07-one-pane-ibm-beige.png` | IBM Beige, a light square-cornered theme: the tile takes the theme's own radius (0) and border colour. |
| `implementer-*-relay-stderr.log` | Relay's own output for each run. |

## Measurements

**Nothing reacts to the pointer** — `1b270ef`'s acceptance, which this change must not undo.
Cropping both two-pane frames to the band the button rows occupy (`1400x40+0+38`) and comparing:

```
before button rows only: 0 differing pixels
after  button rows only: 0 differing pixels
```

Over the whole 1400×820 frame both builds differ by the same 36 pixels between the two shots (the
caret in the focused pane), so the change adds no pointer sensitivity of its own.

**The metal and plastic face follows.** Those two material stylesheets list every raised chip that
takes the theme's gradient; `QFrame#paneChrome` is back on both lists, so the tile is not the one
flat rectangle in a Metal or Plastic window.

**The bevel list is a known leftover, not fixed here.** `1b270ef` dropped
`QFrame#paneChrome[hot="true"]` from the metal and plastic lists but left it in the bevel one, and
nothing has set the `hot` property since — so that rule has never fired, and on a Bevel theme the
row also lacks the moulded edge. The one-word fix sits inside a hunk that session `activepane` is
also editing (their `QWidget#pane[relayActive="true"]` line is seven lines below it, inside the same
diff hunk), so landing it here would have carried half of their change. It is card #BVL1 and lands
on its own once that hunk clears. Measured cost of leaving it: the IBM Beige row gets the tile and
the outline either way; only the moulded edge differs (15792 pixels in the 4x crop).

**A hovered button is not darker than the row.** `@raised` is the top of the ground stack
(Dark Copper: `#241c18` raised, `#15161a` surface), so the old
`QToolButton#paneChromeButton:hover { background: @surface; }` would have made a hovered button
darker than the tile it now sits on. The rule drops the ground and lifts by
`color: @text; border-color: @borderStrong` instead.

## Tests

`tests/themeswitch_test.cpp::thePaneButtonRowKeepsTheTileAndTheOutline` over Dark Copper, IBM Beige
and Relay Dark: the tile's background and outline are the theme's own `surface_raised` and `border`,
the hover rule sets no background, and the metal / plastic chip lists carry `QFrame#paneChrome`.

On the scratch tree it **fails at the base version and passes with the change**:

```
# base Theme.cpp
FAIL!  : ThemeSwitchTest::thePaneButtonRowKeepsTheTileAndTheOutline() ... returned FALSE. (dark-copper)
Totals: 2 passed, 1 failed

# with the change
Totals: 10 passed, 0 failed
```

`relay-theme-tests` (36 passed) and the full `relay-themeswitch-tests` (12 passed) also run green in
the shared `build/`.
