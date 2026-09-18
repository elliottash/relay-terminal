---
id: P43F
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'No shipped button label paints wider than its button in any theme, proved by `relay-buttonfit-tests` (which fails its own self-check if the measurement stops measuring); a non-Claude QA session confirms the API keys modal reads correctly'
source: 'owner in chat, 2026-09-18, with a screenshot of the API keys modal: "in the ''model keys'' modal, this button text is going off the button" and "if easy, see if we can algorithmically check all buttons in case there are others"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-button-labels-clipped/'], related: [], github: null}
---
# Button labels painted past the button's edge

## What was wrong

`src/Theme.cpp` styled the default button with

```
QPushButton:default, QPushButton#primary { background: @accent; …; font-weight: 600; }
```

`QStyleSheetStyle` folds a stylesheet rule's font into the widget's font **only for rules with no
pseudo-state**. So `:default` painted the label demibold while `QPushButton::sizeHint()` went on
measuring it at regular weight: the button asked the layout for a width some 4–14px too small and
clipped its own text. `#primary` is an id selector with no pseudo-state, so bold there was already
reflected in the size hint and never clipped.

This was never specific to one modal. Inside a `QDialog` push buttons are `autoDefault`, so the
`:default` state follows the keyboard focus from button to button — any label long enough was one
Tab away from clipping. The API keys modal hit it because `Add / replace…` is the first push button
in the dialog and therefore the default one on open.

## The change

- `src/Theme.cpp`: split the rule. `QPushButton:default` keeps the accent fill, text and border and
  no longer changes the font; `QPushButton#primary` keeps its bold, which sizes correctly. A comment
  records the Qt rule so the weight is not put back on a pseudo-state.
- `tests/buttonfit_test.cpp` + `CMakeLists.txt`: a new `buttonfit` test (offscreen, runs in ctest).

No other rule in the stylesheet sets a font or another metric property on a pseudo-state; checked by
scanning `src/Theme.cpp` for metric properties (`font-*`, `letter-spacing`, `padding`, `min-width`)
inside `:default/:hover/:pressed/:checked/:focus/:disabled/:selected` selectors.

## The algorithmic check

`tests/buttonfit_test.cpp` answers the "are there others" half of the report, and keeps answering it:

1. It scans `src/*.cpp` for every `QPushButton` label written in the sources (36 today) and adds the
   two widest labels that are built at runtime — 38 labels, 114 rendered rows.
2. For each label it builds a real button under the **live application stylesheet** in each state the
   stylesheet styles differently — plain, `:default`, `#primary`.
3. It measures painted ink, not metrics: it renders the button at its own `sizeHint()` width and
   again with 400px of room, subtracting a render of the same button with an empty label to isolate
   the glyphs from the fill, border and rounded corners. A label that paints narrower at its natural
   width than it does given room is being clipped.

Measuring ink rather than font metrics is what makes it style-independent: it catches a clipped label
whatever the cause — a bold pseudo-state, a padding change, a new theme, a longer label.

`detectsClipping()` guards the measurement itself with a label that plainly does not fit, so the
sweep cannot pass by being blind.

## Evidence

`docs/qa_evidence/2026-09-18-button-labels-clipped/`

- `implementer-before.png` — the modal's button row rendered with the old rule restored: the `A` is
  cut off at the left and the ellipsis at the right, matching the owner's screenshot.
- `implementer-after.png` — the same row after the change, label complete and padding even.
- `implementer-buttonfit-before.txt` — the new test against the old stylesheet: 80 passed, **37
  failed**, every failure a `:default` row, every `plain` and `#primary` row passing.
- `implementer-buttonfit-after.txt` — after the change: 117 passed, 0 failed.

## QA checklist

- [ ] Open Settings → the API keys modal. `Add / replace…` reads in full, with even padding.
- [ ] Tab through that modal's buttons. The accent fill moves with focus; no label clips at any stop,
      including `Import from Claude Code / Codex`.
- [ ] The default button is still visually distinct (accent fill) even though it is no longer bold.
      Confirm that reads as intended, or say so if the bold is wanted back — it can return as an
      explicit `#primary` object name, which sizes correctly.
- [ ] `ctest --test-dir build -R buttonfit` passes. To see it catch the reported bug, put the old
      rule back without touching the stylesheet:
      `QT_QPA_PLATFORM=offscreen RELAY_BUTTONFIT_EXTRA_QSS='QPushButton:default { font-weight: 600; }' ./build/relay-buttonfit-tests`
      — every `:default` row has to fail and every other row pass.
- [ ] Spot-check another accent button (the board's Ask button, `#primary`) still looks bold.
