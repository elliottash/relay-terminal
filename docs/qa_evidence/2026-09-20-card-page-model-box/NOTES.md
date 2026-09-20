# The Switchboard card page's model box (#BRD3) — 2026-09-20

Owner's report: *"did we lose the model picker in the switchboard agent … it's the one in the
cards, it's different and doesn't have the model picker."*

It was not lost. The box is built in the list tools row and reparented into the page agent's
composer, and an open card hides the whole list page — so the page whose Discuss and Plan are
turns of that very `switchboard` role was the one page that could neither name the model nor
change it. Landed in `318beedb`: `src/BoardPane.{cpp,h}`, tests in `tests/boardmodel_test.cpp`.

## What was driven

`./drive.sh` under Xvfb, with `HOME`, `XDG_*` and `TMPDIR` in a throwaway sandbox and
`RELAY_KEYRING=off` (a run never touches the owner's identity key). The board is a one-card
fixture written with `relay_core.board`; there is no key in the sandbox, so the helper runs on
Relay Free and the box names `relay-main` / `relay-flash` / `relay-lite`.

The binary is the one `scripts/land.py` built from the **exact tree it committed**
(`/tmp/claude-1000/land/cardmodelbox/verify/build/relay`, passed in `RELAY_BIN`), not the shared
`build/relay`, which carries other sessions' in-flight edits.

## The shots

| file | what it shows |
| --- | --- |
| `00-list.png` | the list page for comparison: the box in the page agent's composer, on `Follow Main — relay-main`. |
| `01-card-page.png` | the card page of the fixture card `Alpha plain card`: the reply strip now reads **`Follow Main — relay-main` · `Plan (p)` · `Execute (x)`** — the box right-aligned under the reply box, the three buttons last and in their order. This is the shot the report asked for. |
| `02-picker-open.png` | its list open: `Follow Main — relay-main`, `Flash — relay-flash`, `Lite — relay-lite`, the separator, `relay free`, the separator, `⚙ Model roles…` — row for row what the list page's box offers, because one `helpermodel::State` fills both. |
| `03-narrow-pane.png` | the same page in a ~355 px pane (the window squeezed to 730 px, two panes side by side). `Plan` and `Execute` have shed their keys and are both whole inside the row; the box is what gave the width back. |

## What the narrow shot is checking

The box is the only thing on that strip a layout can shrink — `QSizePolicy::Maximum` against three
`QPushButton`s it can only clip — so `CardDetail::fitButtons` counts the box's *natural* width when
it decides whether the keys stay: the keys come out of the labels first, and only then does the box
squeeze towards its `"MM"` minimum. `theCardsModelBoxGivesTheRowItsWidthBackBeforeAButtonIsClipped`
asserts that at 350 px in the test, and `03-narrow-pane.png` is the same thing on screen.

The squeezed box clips its current text rather than eliding it. That is `CurrentTextComboBox`'s
own behaviour and its twin's — the terminal pane's model box and the list page's have always
clipped in a narrow strip (`src/CurrentTextComboBox.h`, "A squeeze clips the text, as it always
has in a narrow pane") — so the two boxes still look and behave alike, which is the point of them
being one widget kind.

## Tests

- `ctest --test-dir build -R '^board$'` — 86 passed, 0 failed (`theCardPageCarriesTheSameModelBoxAsTheListPage`, `theCardsModelBoxGivesTheRowItsWidthBackBeforeAButtonIsClipped` new).
- `ctest --test-dir build -R '^boardpane$'` — passed.
