---
id: 1NW3
type: work
status: needs-verification
labels: [feature, ui]
assignee: agent
implemented_by: kimi/kimi-k3
session: 58bdcbd6-7c45-4e94-9cbb-9e60b534a2bc
rank: zzzzzzzzzzzzzzzz
created: '2026-09-20'
source: owner, in a Relay pane, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-card-row-fold-link/], related: [], github: null}
---
# A board tool-call line unfolds on click; only the #ID opens the card

## Issue
relay's tool call logs look like this:


▸ commented on card #XH4K · note
▸ read card #XH4K

those are great, but the whole line is a link taking me to the card. i would prefer if i clicked on the #XXX it would link to the card, but if i click on the rest, it would uncollapse and show the regular snippet

## Plan
**Goal.** A board tool-call row ("▸ commented on card #XH4K · note") unfolds its regular call detail when clicked anywhere, and only the `#XH4K` segment opens the card.

**Findings.**
- `backend/relay_core/tool_labels.py::_board` puts `card: "#XH4K"` on the row; `_open` turns it into `open: {type: "card", id: "XH4K"}` (bare id) — no backend change needed.
- `src/CallLines.cpp::anchorsFold` answers false for `Click::Card`, so the row anchors `relay://open-call/…` and a click anywhere opens the card (`Pane.h::openCallTarget`, `Click::Card` → `onOpenCard`).
- `Pane.h::drawCallRow` wraps the whole row in that one OSC 8 anchor.
- `relay://card/<id>` is already a routed link: `Pane.h::openOutputTarget` → `onOpenCard`; `src/OutputLinks.cpp::cardTarget(id)` builds the URI. The engine styles OSC 8 links itself.

**Steps.**
1. `src/CallLines.{h,cpp}`: `anchorsFold` folds a `Click::Card` row when the backend has folds; new pure helper `cardSegment(row, label)` returns the `#ID` span inside the row text, or -1 (id cut away by `fit`, label without one).
2. `src/Pane.h::drawCallRow`: takes the label; a fold-anchored card row with a found segment is drawn "▸ "+prefix under the fold anchor, `#ID` under `relay://card/<id>`, the remainder under the fold anchor again. Call sites (tool_started, tool_result, the live tick) pass the label they already hold.
3. `tests/calllines_test.cpp`: anchorsFold(Card) with/without folds; cardSegment on the finished and the running row, and its -1 cases.
4. `docs/AGENT-SESSIONS-PROTOCOL.md` § 23.6: the card row's click now folds; the #ID is the card link.

**Risks.** The fold layer finds an anchor only when it starts at column 0 — the fold anchor still opens the row; the card link is a mid-line anchor switch, the same mechanism `relay://open-call` already uses for a whole row. A backend with no fold layer keeps today's behavior (whole row opens the card, then the detail fallback).

**Verify.** `ctest --test-dir build -R calllines`; live under Xvfb with an isolated XDG_CONFIG_HOME: a `board_read` row unfolds on a click away from the id and opens the card on the #ID.

## Execution Summary
Built as planned:

- `src/CallLines.{h,cpp}`: `anchorsFold` folds `Click::Card` rows on a backend with a fold layer; new pure `cardSegment(row, label)` returns the `#ID` span in the row's text, -1 when the label names no card or the id was cut away. A backend with no fold layer keeps the old whole-row `relay://open-call` behavior.
- `src/Pane.h::drawCallRow`: takes the label; a card row is drawn `▸ `+prefix under the fold anchor, the `#ID` under `relay://card/<id>` (`relay::links::cardTarget`, routed by `openOutputTarget` → `onOpenCard`), the remainder under the fold anchor. Engine check: `TerminalView::foldAnchorAt` reads the anchor at the click position before `linkAt`, so the segment routes as a link and the rest of the row toggles the fold. No backend (`tool_labels.py`) change needed — the label already carries `open: {type: card, id}`.
- `docs/AGENT-SESSIONS-PROTOCOL.md` § 23.6: the card row folds on a surface that has one; the #ID is the card's link.
- Evidence: `docs/qa_evidence/2026-09-20-card-row-fold-link/` — three screenshots of the live Xvfb run.

## Tests
- `ctest -R calllines` — 56 passed, 0 failed (new: `anchorsFold(Click::Card)` both ways; `cardSegment` on the finished and running rows, cut-away, card-less label, non-card row)
- manual: docs/qa_evidence/2026-09-20-card-row-fold-link/

## QA checklist
- [ ] In a pane with this build, a `board_*` call's row (e.g. `▸ read card #1NW3`) shows the ▸ marker of a foldable row, and a plain click on the row's text — not on the id — unfolds the call's detail in place; a second click folds it away.
- [ ] A click on the `#ID` segment itself opens the card in the Switchboard (and hovering it shows the link cursor/underline the engine gives OSC 8 links).
- [ ] The fold's last row still offers "open in pane", and that path still reaches the card.
- [ ] A `board_create_card` row ("created card “…”", an id the title does not show) unfolds like any other row and has no dead link segment.
- [ ] Ctrl+Shift+L link walking finds the card link on the row; the row's own fold is not offered as a walk stop.
