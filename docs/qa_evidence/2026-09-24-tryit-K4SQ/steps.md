# Steps — #K4SQ Try it

1. **check** — `QT_QPA_PLATFORM=offscreen ./build/relay-boardpane-tests aCardLinkOpensTheBoardOnTheCardAlone`
   passed: a 995-wide view (above `kCardSplitWidth` = 900) keeps the list beside an ordinary
   `openCard`, hides it for `openCardSolo`, keeps it hidden across an in-board link, and restores
   it after `closeDetail`. All 16 boardpane tests passed; the landed tree built.
2. **agent** — staged `/tmp/claude-1000/tryit/k4sq` (display `:187`, 2200x1200, sandboxed HOME,
   no model, no network) and drove the link click through the app's named `open` seam
   (`relay-drive open AA01` — the same `openBoardCard()` a chat `#ID` uses), because a clickable
   chat link needs a model turn and staging has none. Read through named controls:
   `boardKeys` says "Esc back to the board…" while the link's card is open (card has the pane to
   itself, ~1050px wide board pane), and "Enter open…" after `boardCardClose` (list is back).
   Shots: `02-link-click-card-alone.png`, `03-esc-back-to-the-list.png`,
   `04-restaged-for-the-person.png`. No pointer input was needed and no code was changed to
   stage this.
3. **person (~2 min)** — the mouse steps a named seam cannot do, and the judgement:
   1. click the `#AA02` reference inside AA01's body,
   2. press Esc once,
   3. open AA02 by hand from the list,
   and answer the card's question.

Report: check 1, agent 1, person 1 — the in-board link, the by-hand open and the feel of the
two layouts are the person's judgement; everything mechanical that had a named seam is done.
