---
id: Y2F4
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
implemented_by: deepseek/deepseek-v4.1-flash
session: e273439f-004b-4369-b20d-b81d11a21ec0
rank: zzzzzzzzzzzzzzzzi
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-switchboard-hash-toast/], related: [3ZAP], github: null}
---
# Switchboard: a hashtag click-copy should send a notification

## Issue
when you copy to keyboard in the switchboard from clicking on a hash code, send a notification "#xxxx copied" (like the highlight-text copy notification)

## Decisions
Owner, 2026-09-20, choosing from three options (add a toast to every hashtag copy / make card `#ID`s copy too / chase a missing notification): "Add the toast too".

So the copy keeps the board's own "Copied #bug" notice line and *also* raises the copy-on-highlight-style toast, reading "#bug copied". A `#ID` reference in text still zooms (no copy), and the list rows' `#ID` column is still not a click target — both unchanged from #3ZAP's owner decision.

## Tasks
- [x] BoardView::toast + placeToast: the pane's popup, on the board (#Y2F4) <!-- t:0x -->
- [x] copyTag raises the toast beside the notice <!-- t:qg -->
- [x] placeToast on resize, so the popup keeps its corner <!-- t:ew -->
- [x] Unit test: the toast's text and its bottom-right place <!-- t:sf -->
- [x] Live Xvfb drive: four copy surfaces, the fade, the reference zoom <!-- t:zn -->
- [x] docs/ARCHITECTURE.md: the board's own toast <!-- t:ts -->
- [x] Land through scripts/land.py, my hunks only — 123ecb67598d <!-- t:0h -->


## Execution Summary
A hashtag copy in the Switchboard now says so twice: the board's notice line keeps "Copied #bug", and the copy-on-highlight-style toast appears at the pane's bottom-right reading "#bug copied".

- `src/BoardPane.h`, `src/BoardPane.cpp` — `BoardView::toast(text, ms = 1600)` and its `placeToast()`: one `QLabel` built on first use, wearing the `toast` object name the theme already styles for a pane's popup, shown for 1.6 s and re-anchored on a resize. The board is a `ToolPane`, not a `Pane`, so it cannot reach `Pane::toast`; this is the board's own. `BoardView::copyTag` (a list-row label badge, the card meta's `#label`, a `#tag` in the body or thread, and the Ctrl+C card reference) calls it beside the notice.
- `tests/boardmodel_test.cpp` — `hashtagClicksCopyAndCardRefsZoom` also pins the toast's text and its place at the pane's bottom-right.
- `docs/ARCHITECTURE.md` — a paragraph beside the copy-on-highlight surfaces: copying something that is not a selection says so the same way.
- `docs/qa_evidence/2026-09-21-switchboard-hash-toast/` — the Xvfb drive, its screenshots and `ocr.txt`.

One thing the live drive caught that the unit test had not: the first cut placed the popup before showing it, and `placeToast()` asks whether it is up — so the first show left it at the widget's top-left. Showing before placing fixed it, and the unit test now asserts the corner.

## Tests
- `./build/relay-board-tests hashtagClicksCopyAndCardRefsZoom` — PASS (the `tag:`/`card:` anchors, the clipboard, the notice, the toast's text and its bottom-right place, the row badge's copy-without-selection).
- The whole board suite on this checkout: **96 passed, 0 failed** (`./build/relay-board-tests`, offscreen).
- `docs/qa_evidence/2026-09-21-switchboard-hash-toast/drive.sh` under Xvfb: **18 passed, 0 failed** — the four copy surfaces each copy, notice and toast; the toast has faded 2.4 s later while the notice still stands; a `#KAN3` reference still zooms without touching the clipboard.

## QA checklist
- [ ] Re-run the drive on a quiet tree: `docs/qa_evidence/2026-09-21-switchboard-hash-toast/drive.sh` — 18/18, and read `ocr.txt` rather than trusting the count.
- [ ] By hand: click a list-row label badge, the card meta's `#label`, a `#tag` in the body and one in the thread. Each must put `#tag` on the clipboard, leave "Copied #tag" in the notice line, **and** show "#tag copied" at the pane's bottom-right — not at its top-left.
- [ ] By hand: the toast is gone within about two seconds while the notice line is still there; a resize with the toast up keeps it in the corner.
- [ ] By hand: Ctrl+C (or `y`) with a card selected still copies `#ID` and toasts "#ID copied".
- [ ] Unchanged: a `#ID` reference in the card's words or the thread zooms to that card and leaves the clipboard alone; the list rows' `#ID` column is not a click target.
- [ ] The toast is the pane's own look — same `#toast` rule, same rounded popup, no new stylesheet — and nothing else in the board's notice area moved.
