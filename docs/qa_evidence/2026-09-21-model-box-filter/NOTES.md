# The model box and the level box: the current row is highlighted, and you can type at it

Owner, 2026-09-21: *"when you use alt+m or alt+e, your current selection should be highlighted.
then you should be able to select with up/down arrows, and also filter with text typing (like
warp's model picker)."*

Implementer evidence. Everything below was driven live under Xvfb by `drive.sh`, against a build
of the exact tree this change lands (a clean `git archive main` plus the changed files — the
checkout's `build/` was held by another session's in-flight edit at the time).

## Why nothing was highlighted

Not the rows, not the combo's current index, and not the theme. The box was always on the right
row: re-opening it after picking Flash put the marker on Flash. The marker was a **1px focus
rectangle and no fill at all** (`before-altm-open.png`, `before-after-down.png` — Down moves a
hairline from "Main agent" to "Flash agent" and nothing else changes).

`relay::theme::applyTheme` sets `QStyleFactory::create("Fusion")`. Fusion answers
`SH_ComboBox_Popup` with **1**, so Qt draws a combo's list as a *menu*: `QComboBoxPrivate::
updateDelegate` installs `QComboMenuDelegate`, which paints each row through `CE_MenuItem`. A menu
item painted on a `QListView` matches none of the stylesheet's `QComboBox … QAbstractItemView`
declarations, so `selection-background-color: @accent` (src/Theme.cpp:524) is never read. The row
*is* selected — the selection model says so — it is filled with `#1d1613` on a `#241c18` list.
A 3% step. Invisible.

`old-popup-probe.cpp` is that measurement, kept so it can be re-checked; `old-popup-probe.txt` is
its output on this machine (Qt 5.15.13, theme Dark Copper):

```
default style (breeze)    SH_ComboBox_Popup 0  QItemDelegate       current row #533927  other #241c18
Fusion (what Relay sets)  SH_ComboBox_Popup 1  QComboMenuDelegate  current row #1d1613  other #241c18
```

So the fix is not a colour: a list whose highlight depends on a style hint will lose it again the
next time the style changes. `relay::FilterPopup` (src/FilterPopup.{h,cpp}) paints its own rows
from the theme tokens, with no style hint in the path — and, since it is our own list, it carries
the filter line the owner asked for. `CurrentTextComboBox::showPopup` opens it instead of Qt's,
reading the rows out of the combo's own model, so nothing that *fills* a box had to change.

## Shots

| | |
|---|---|
| `a-strip.png` | the composer strip before anything is opened |
| `b-altm-open.png` | **Alt+M**: `glm-5.3 (main)`, the pane's model, highlighted at the top of the list, with the filter line above it |
| `c-down-down.png` | after **Down Down**: the highlight has moved two pickable rows down |
| `d-typed-kimi.png` | typing **kimi**: the text is on screen in the filter line, the list is down to the four rows that match, and the first of them is highlighted |
| `e-picked.png` | **Enter**: the box now reads `kimi-k3 (main)` and the pane says "Model: k3 · conversation kept" |
| `f-no-match.png` | a filter nothing matches (**zzzq**): a quiet "no match" line, and the list shrinks to it rather than standing empty |
| `g-alte-open.png` | **Alt+E**: the same control over the reasoning levels, with the pane's own level (`high`) highlighted |
| `h-alte-typed.png` | **hi** typed at it: four levels down to one |
| `i-escaped.png` | **Escape**, then typing: "back in the prompt box" lands in the prompt box — the caret goes back where Alt+E took it from |
| `j-mouse-open.png` | the same list opened by **clicking** the box: same highlight, same filter line, and the row under the pointer takes the highlight as the pointer moves |
| `k-altm-again.png` | **Alt+M** a second time closes the list, as it has since 2026-09-20 |
| `before-*.png` | the box as it was: no fill on any row, only a hairline |

The first cut of the shots had the list's geometry wrong in three ways, all of them visible in the
Alt+E one and all fixed before landing.

1. **It scrolled over two rows.** The height is now the row heights the delegate really returns
   plus the filter line's own, both children pinned with `setFixedHeight` so the layout has nothing
   to redistribute, and the scrollbar switched *off* rather than left to `ScrollBarAsNeeded` — a
   few pixels short and "as needed" means "always". Only past fourteen rows, or past the room the
   window leaves, does the list scroll, and then the bar's width is added to the box instead of
   taken out of the rows.
2. **A list that grew stayed scrolled.** kimi offers three levels and the pane was on the middle
   one, so the short list had scrolled down to reach it; once it was tall enough for all three it
   kept the offset and drew "high", "max" and a row of empty ground, with "low" off the top. The
   scroll is now reset after the resize, not before it.
3. **It hung off the right of the window**, because the level box sits at the end of the strip.
   The box is clamped to the window it hangs from (intersected with the screen), left-aligned with
   the picker as before and right-aligned to it when that would overflow. The width floor went with
   it: no narrower than the picker, no wider than its own longest row, counting the filter line's
   own words as content.

Measured on the shots that landed: the three-level list is 117px for a 31px filter line and three
28px rows, nothing over and nothing under, and its right edge is x=1367 in a window that ends at
1399. `aListThatFitsDoesNotScroll`, `agrownListIsNotLeftScrolled` and `theListStaysInsideTheWindow`
hold all three.

## Unit test

`tests/filterpopup_test.cpp`, `ctest --test-dir build -R filterpopup` — 11 cases, all passing:

- `currentRowIsHighlightedOnOpen` — the row the caller calls current is the highlighted one, and a
  separator or a switched-off row never can be.
- `theHighlightIsAVisibleBand` — the popup is rendered and its pixels measured, so the invisible
  highlight cannot come back through a delegate change.
- `arrowsSkipSeparatorsAndDisabledRows` — Down over a separator and over a row with no key; the
  ends hold.
- `typingFiltersAndHighlightsTheFirstMatch` — case-insensitive and fuzzy (`relayFuzzyScore`), the
  first match highlighted, "more models…" reachable by its own words, clearing brings the rows back.
- `noMatchSaysSoAndPicksNothing`, `enterReturnsTheOriginalRowIndex` (the index in the list as it was
  handed in, not the filtered position), `escapeAnswersWithNothingAndClearsTheFilter`,
  `theEffortListIsTheSameControl`.
- `aListThatFitsDoesNotScroll` — two rows, and one after filtering, with no scrollbar, every row
  drawn whole, and a box that is exactly its layout's size hint; forty rows still scroll.
- `agrownListIsNotLeftScrolled` — three levels with the middle one current: all three are drawn.
- `theListStaysInsideTheWindow` — a picker at the far right of the strip: the list is right-aligned
  to it, its right edge is inside the window, and it is never narrower than the picker.

## What a QA pass should check

1. Alt+M in a pane with a provider: the row the collapsed box names is the highlighted one, and it
   is scrolled into view when the list is long enough to scroll.
2. Down/Up move it and step over the group separators; Enter picks; the box and the pane follow.
3. Type part of a model's name: the text appears in the filter line, the rows narrow, the first
   match is highlighted, Backspace widens it again.
4. Escape: nothing changed, and the next thing typed goes into the prompt box.
5. Alt+E on a model with levels, and Alt+E on one without (it should still say "This model has no
   reasoning setting.").
6. Mouse only: click the box, hover the rows, click one.
7. A picker at the right-hand end of a narrow pane: the list must not hang off the window, and a
   list short enough to draw whole must show no scrollbar.
8. The Switchboard pane's model box, which is the same widget, behaves the same way.

## Notes on the run

`drive.sh` sets `isolation/enabled=false` in the sandbox profile. That is not part of the change:
under an isolated `XDG_RUNTIME_DIR` there is no `systemd --user` to reach, the transient scope
fails and the agent worker never starts — and with no worker there is no model catalog, so the
list would have had four rows and nothing to filter. The screenshots above were taken with the
worker running and no key of any kind: `RELAY_GLM_CODING_API_KEY` and `RELAY_KIMI_CODE_API_KEY`
are literal non-key strings, `RELAY_KEYRING=off`, and no turn is ever submitted.
