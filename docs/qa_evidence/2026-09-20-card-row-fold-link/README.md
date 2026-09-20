# #1NW3 — a board tool-call line unfolds on click; only the #ID opens the card: implementer evidence

**Date:** 2026-09-20 · **Executing session:** Relay agent pane, `kimi/kimi-k3` (session
`58bdcbd6`) · **Tree:** `0c63c2a9e94d` (the tip `scripts/land.py begin` snapped) plus this
card's hunks.

This is implementer evidence, not a QA verdict.

## What the card asked

A board call's row (`▸ commented on card #XH4K · note`) was one whole-line link to the card.
Only the `#XH4K` should open the card; clicking the rest should unfold the call's regular
detail.

## What changed

- `src/CallLines.{h,cpp}` — `anchorsFold` answers true for `Click::Card` on a backend with a
  fold layer (a backend without one keeps the whole-row `relay://open-call` anchor, unchanged);
  new pure `cardSegment(row, label)` finds the `#ID` span in the row's text, -1 when the label
  names no card or the id was cut away on a narrow pane.
- `src/Pane.h::drawCallRow` — takes the label its callers already hold; a card row with a found
  segment is drawn in three parts: `▸ ` + prefix under the fold anchor, `#ID` under
  `relay://card/<id>` (`relay::links::cardTarget`, the scheme `openOutputTarget` already routes
  to `onOpenCard`), the remainder under the fold anchor again. The fold anchor still opens the
  row at column 0, which is where the fold layer looks for it.
- Engine check (no change needed): `TerminalView::foldAnchorAt` reads the hyperlink at the
  *click position* and only then falls to `linkAt`, so a click on the card segment routes as a
  link and a click anywhere else on the row toggles the fold (`engine/view/TerminalView.cpp:1446`,
  `:2320`).
- `tests/calllines_test.cpp` — `anchorsFold(Click::Card)` both ways; `cardSegment` on the
  finished and the running row, the cut-away case, a card-less label and a non-card row.
- `docs/AGENT-SESSIONS-PROTOCOL.md` § 23.6 — the card row folds on a surface that has one;
  the #ID is the card's link.

## Automated

```
QT_QPA_PLATFORM=offscreen build/relay-calllines-tests     # 56 passed, 0 failed
```

## Live run (Xvfb :231, isolated `XDG_CONFIG_HOME=/tmp/relay-1NW3/config`, `--fresh`, Relay Free pane)

A Relay Free pane in this workspace was asked to run `board_read` on #1NW3, leaving the row
`▸ read card #1NW3` on screen. Screenshots (OCR-read, `tesseract`):

1. `01-row-at-rest.png` — the collapsed row among the turn's lines.
2. `02-click-off-id-unfolds.png` — a plain click on the row's text (on "read", away from the
   id): the row is now `▾` and its fold hangs beneath (`id: 1NW3`, `open in pane`). The card did
   **not** open.
3. `03-click-id-opens-card.png` — a click on the `#1NW3` segment: the Switchboard opened beside
   the pane with card #1NW3's detail (`#1NW3 — "A board tool-call line unfolds…"`, Issue, Plan).

Both clicks did what the card asks.
