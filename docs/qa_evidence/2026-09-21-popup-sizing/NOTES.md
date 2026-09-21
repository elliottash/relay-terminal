# The popup list is sized for the rows it actually draws — #MDL1, 2026-09-21

`relay::FilterPopup` — the list Alt+M and Alt+E drop open — measured its list **one row shorter
than it drew it**, but only once the application's stylesheet was on it. Two agents reported it on
the same day (`docs/qa_evidence/2026-09-21-effort-by-model/NOTES.md`, "One thing found here that is
not this card's to fix"):

* the last row of a four-level Alt+E list was clipped by a few pixels, drawn flush against the
  frame;
* a list that got **shorter** between two opens stayed scrolled past its first row — Alt+E on a
  pane with four levels, then on a pane with three, drew `high` and `max` with `low` off the top
  while the box itself held all three. Persistent: a second Alt+E drew the same two.

The offscreen unit test `aShorterListOnTheNextOpenDrawsEveryRow` passed throughout, which is the
part worth keeping in mind: the stylesheet is not on the popup in a test, and the stylesheet is the
whole of it.

## What was wrong

The popup is built as a child of the combo box it hangs from (`CurrentTextComboBox::filterPopup`,
`new relay::FilterPopup(this)`). So the application stylesheet's

```
QComboBox QAbstractItemView { background: @raised; … border: 1px solid @border; padding: 4px; … }
```

(src/Theme.cpp:386) matches the popup's list as a **descendant of the box**. The popup's own rule,
`QListWidget#filterPopupList { background: transparent; border: none; outline: none; }`, takes the
border back — but nothing takes the `padding` back, and a padding on a `QAbstractScrollArea` is
answered as `PM_DefaultFrameWidth`. The styled list therefore reports `frameWidth() == 4` and
spends **8 px of its own height** before a row is drawn.

`layoutForAnchor` set the list **widget** to the sum of the delegate's row heights:

```cpp
const int listHeight = std::min({std::max(wanted, 1), cap, room});
m_list->setFixedHeight(listHeight);
```

so the rows got a viewport 8 px shorter than they needed. Three consequences, all of them in the
shots below:

1. the last row lost the bottom of itself — at 26 px rows and 8 px missing, enough to shave a
   descender: `xhigh` reads `xhiah` in `before-a-alt-e-four-zoom.png`;
2. the list decided it had to scroll, so the scrollbar policy went to `AsNeeded` and
   `scrollToItem` on the current row moved it. With the current row the *last* one — which is what
   `/effort xhigh` then `/model kimi-k3` leaves you with, because kimi stops at `max` — one whole
   row went off the top;
3. that offset was still there the next time the same popup opened.

The popup's *total* height was right all along: the old code added the list's frame to the popup
(`chrome = 2 * frameWidth() + 2`) but not to the list, so the 8 px sat as dead ground under the
last row while the rows were squeezed. That is why every popup below is exactly as tall before as
after, and only the split between the list and the slack changed.

## The fix

`src/FilterPopup.cpp`. `PopupList` is a `QListWidget` that will answer the one measurement
`QAbstractScrollArea` keeps protected — `2 * frameWidth() + viewportMargins().top() + .bottom()`.
`layoutForAnchor` now sizes the **viewport** to the rows and the widget to that plus the chrome,
asked of the polished widget rather than assumed, because it is the style's number: it is 0 with
no stylesheet on the popup and would be something else again under another theme. The same number
goes into the width, so a name is not elided eight pixels early either.

The scroll reset became `settleScroll()`, which runs after the resize on **every** open — from
`layoutForAnchor`, and again from `showEvent`, because a hidden widget's resize event is *posted*,
not delivered, so the scroll range the list has while it is being sized is not the range it has
when it is drawn. It puts the list at the top and scrolls down only as far as the current row
needs. `showEvent` also lays out once more against the real geometry, so if a style ever spends
more than `PM_DefaultFrameWidth` said it would, the measured number wins.

## The unit test

`tests/filterpopup_test.cpp :: theAppStylesheetDoesNotCostTheListARow` is the case that reproduces
it headlessly: it calls `relay::theme::applyTheme(*app)` — the call `main()` makes — and opens the
list off a real `QComboBox#statusPicker`, which is what `Pane` names the level box and what puts
the list inside the `QComboBox QAbstractItemView` rule. It asserts the invariant rather than the
arithmetic: every row of a list that fits is drawn whole, the list does not scroll, and a list past
the cap draws whole rows from the top. The test target now links `relay-highlight`, which carries
`src/Theme.cpp`.

`unit-before.txt` is that test file against the **unfixed** `src/FilterPopup.cpp`
(`git show main:src/FilterPopup.cpp`, built by hand against the same test source) and
`unit-after.txt` is the same against this change:

```
before   FAIL  four levels: row 0 is not drawn whole
         FAIL  three levels after four: row 0 is not drawn whole
         FAIL  the top of a scrolling list: row 0 is not drawn whole
         FAIL  the last match is not drawn whole
         Totals: 22 passed, 4 failed
after    Totals: 23 passed, 0 failed
```

`ctest --test-dir build -R filterpopup` passes in the checkout.

## Driven live under Xvfb

`drive.sh` runs the same five steps against two binaries and `measure.py` measures the shots;
`measured.txt` is its whole output. Both binaries are built from a clean `git archive main` export
— BEFORE is `main` at `2907379e`, AFTER is that plus `src/FilterPopup.{h,cpp}` — because the
checkout's own `build/` would not link at the time (another session's `src/ModelsPane.cpp` was
mid-edit). Isolated `HOME`/`XDG_*`/`TMPDIR` under a short path (the 108-byte socket limit),
`RELAY_KEYRING=off`, `isolation/enabled=false`, fake provider keys, and no turn is ever sent:
every step is a model switch, a level pick or a list opening, which the worker answers by itself.

The popup is an override-redirect X window with no title, so its rectangle is **asked of X** —
the one visible `relay` window that is not the main one — and not guessed from the pixels: where
the list hangs over the composer strip its ground *is* the strip's ground, and both a diff of the
closed and open shots and a hunt for the border measure the wrong box there. Inside that rectangle
`measure.py` counts bands of ink: a row scrolled off the top is a band that is not there, and a
clipped row is a band with its descenders shaved.

| step | list | before | after |
| --- | --- | --- | --- |
| `a-alt-e-four` | openai's four levels, `high` current | popup 110x143, 4 bands, the last 10 px tall — `xhigh`'s descender gone, 9 px under it | 118x143, 4 bands, the last **13 px** — whole — and 10 px under it |
| `b-alt-e-three` | kimi's three after those four, `max` current | 110x117, **2 bands**: `low` scrolled off the top, 27 px of empty ground under `max` | 118x117, **3 bands**: `low`, `high`, `max` |
| `c-alt-e-three-again` | the same list, opened a second time | identical to `b` — not a first-draw transient | identical to `b` — all three |
| `d-alt-m-long` | Alt+M: three class headers, four models, a separator and "more models…" | 227x254, 8 bands, 9 px under the last | 235x254, 8 bands, **13 px** under the last |
| `e-alt-m-filtered` | `glm` typed at it: four rows | 248x143, 4 bands, the last 10 px — `glm-5.3-flash` shaved | 256x143, 4 bands, the last **13 px** |

Every popup is the same height before and after, and 8 px wider after — the width the rows used to
lose to the same padding.

Per step there is the screen with the list closed (`-closed.png`), the whole window with it open
(`-open.png`), the bottom 470 px of that (`-open-box.png`), the popup alone at three times life
size (`-zoom.png`), its geometry (`-popup.txt`) and the root window's children at that moment
(`-windows.txt`). The two to look at first are `before-b-alt-e-three-zoom.png` — two rows where the
box holds three — and `after-b-alt-e-three-zoom.png`.

## What a QA pass should check

1. Alt+E on a pane whose model has four levels, with the *last* of them current: every level drawn,
   nothing above the first row cut off, nothing under the last.
2. Then switch to a model with fewer (`/model kimi-k3` after `/effort xhigh`) and Alt+E again —
   this is the reported fault, and it needs the level to snap onto the last row of the shorter
   list. Open it twice.
3. Alt+M, and Alt+M with something typed at it: the last row of whatever is left must have the same
   clear ground under it as the first has above it.
4. A list long enough to scroll (a provider with more than fourteen models listed): it opens at the
   top unless the current row is further down, and the rows it shows are whole ones.
5. Another theme, and a `QComboBox QAbstractItemView` rule with a different padding, should all
   still do the above — nothing in the sizing is a constant now.
