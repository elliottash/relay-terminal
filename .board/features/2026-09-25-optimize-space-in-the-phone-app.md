---
id: 8R3V
type: work
status: executing
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude:ashe-ethz-ch
session: 47171b5e-f82e-4bcd-a055-bf22f59d39e7
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: visual, primary: script, also: [ai-visual, person], human: optional, criteria: 'At 390x844 in the real-Chrome harness: read mode shows one slim header and the terminal down to the bottom edge with a floating prompt button; the button opens a write sheet with no terminal; X and Back close it; Send closes it; long lines wrap to the phone width on the normal screen. Owner confirms on an iPhone that the keyboard sits under the prompt box.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'remote: iOS Safari'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Optimize space in the phone app.

## Issue
Optimize space in the phone app. 

In the pain have a very small header that allows you to get back and then showed the terminal material and board wrap should be adapted to the phone screen then at the bottom right that she used to be a small button that will open the prompt and then when the prompt is open, you don’t even see the terminal. You just see the prompt box and then your keyboard and then there’s like an X button or the back button to get back to the thing to see what it says.

## Discussion points
Mapping of the request onto `app/` (the phone web app), 2026-09-25.

**What a pane screen stacks today** (`app/index.html`, `app/style.css`): the global `#bar`
(desktop name + two status chips) above `#thread-bar` (back ‹, title, cwd, model) above the
thread transcript (`#thread-body`) and pane view (`#pane-view`), then the terminal
(`#screen-wrap`) with its own `#term-bar` (Watching chip, A−/A+, Keyboard, Take over, note),
then `#secret-row`/`#outbox-note`, and a composer (`#composer`: textarea + mic + Send) docked
at the bottom at all times. Six fixed regions before any terminal content; only `.short-viewport`
(keyboard up) trims some of it away.

**The proposal, mapped to two modes:**

1. **Read mode (default).** One very small header — back button and little else (cwd/model/chips
   tuck away) — and the terminal material fills the screen. No docked composer.
2. **Board on the phone.** The board screens (`app/board.js`, `board.css`, `#screen-board`)
   re-wrap for a narrow phone screen rather than shrinking the desktop layout.
3. **Small floating button, bottom right** replaces the docked composer; it opens the prompt.
4. **Write mode (prompt open).** Terminal hidden entirely: just the prompt box + the keyboard,
   with an X / back button to close. Closing returns to read mode, where the answer prints —
   "get back to the thing to see what it says".

**Places the details live, to decide in Plan:**

- Where `#term-bar`'s controls go in read mode (Take over, Keyboard, font size): overflow in
  the small header, or a tucked row behind the floating button.
- Whether the thread transcript shares read mode with the terminal (scrollable above it) or
  the terminal alone is the material.
- The write sheet inherits the composer's extras: voice clip mic, outbox note, password
  (`#secret-row`) — those must not lose their place.
- Guests (`app/guest.js`) have a parallel docked composer of their own; same treatment or not.
- Keyboard-on/`viewport.js` interplay: the write mode is exactly the case `--app-height`
  exists for, so the sheet sits on the visual viewport, not under the keyboard.

## Done means
On a phone-width screen (< 600 CSS px), opening a pane shows **read mode**: one slim header (back ‹ and the pane's title, about 44 px) and the terminal filling the rest of the screen down to the bottom edge. There is no docked prompt box, no desktop-name bar while the link is up, and a small round prompt button at the bottom right. Tapping the button opens **write mode**: the terminal is gone, only the prompt box (with its model/mic/send strip) and the keyboard are shown. An X (or the phone's Back) returns to read mode with the draft kept, and sending returns to read mode where the answer prints. Long lines on the normal screen wrap to the phone's width instead of scrolling sideways.

Failure looks like today's stack: `#bar`, a two-to-three-line `#thread-bar`, `#term-bar` and the docked prompt box all on screen around a terminal that has only a third of the height. Other failures: writing with the terminal still squeezed above the keyboard, or Back leaving the pane when a write sheet was open. Tablets and desktop browsers (≥ 600 px) look exactly as they do now.

## Plan
**Goal.** Give the phone two modes on a pane: *read* (slim header + terminal, floating prompt button) and *write* (prompt box + keyboard only, X to close). Wrap the terminal's text to the phone width. Tablets and desktop browsers stay exactly as they are.

**Reading of the request.** "board wrap should be adapted to the phone screen" is taken as dictated *word wrap*, because it sits between "the terminal material" and the prompt button. The Board screens (`app/board.js`) are already phone-first: their own 44 px bar, one column. So the Board only loses the global `#bar`, like the pane does. If the owner meant the Board's layout, that is a separate card.

**Findings.**
- A pane screen stacks `#bar` (`app/index.html`) → `#thread-bar` (back, title, cwd, model) → `#pane-view`. The pane view holds the terminal, `#term-bar`, thinking, queue, asks/ask and `.rp-composer` (`app/pane.js` ~L230–360). After it come `#thread-note`, `#secret-row`, `#outbox-note` and `#composer`. `#composer` is the legacy box; `ensurePaneView()` hides it once the pane view mounts (`app/app.js` ~L976–1010).
- The microphone already moves into the pane composer's `hostSlot`, so it goes wherever `.rp-composer` goes. cwd and model already sit in the composer's strip (`folderChip`, `model`), so the header can drop them in read mode without losing them.
- Back is a layer stack, `closeOneLayer()` in `app/app.js` ~L1118 (pane sheet → board sheet → card → pane), with `syncBackEntry()` keeping one spare history entry.
- Only `.short-viewport`/`.tiny-viewport` (`app/viewport.js`, `style.css` ~L574–581) trim chrome today, and only while the keyboard is up.
- `app/screen.js` draws the desktop's `cols` as a grid: `fit()` scales the font down to a 12 px floor, then scrolls sideways (~L254–285). Its scroll maths measures real row heights (`offsetHeight`, ~L664–679), so rows may take more than one line.
- Harness: `tests/test_remote_browser.py` drives real Chrome over CDP at `PHONE = (390, 844)` against the demo desktop, and `RELAY_SHELL_SHOTS` keeps screenshots.

**Steps.**
1. **Mode state** (`app/app.js`). Add `phoneLayout()`, true when `matchMedia('(max-width: 599px)')` matches (pane.js's own phone threshold), and a `writeMode` flag. Set `#screen-thread[data-mode=read|write]` only while `phoneLayout()` is true; otherwise remove the attribute so none of the new CSS applies. Opening a pane starts in `read`; `closePane()` resets it.
2. **Read mode CSS** (`app/style.css`, `app/pane.css`). Under `[data-mode=read]`:
   - hide `#bar` while `#link-status.ok` (use `:has()` as the short-viewport rule already does, so "offline" is still said), and do the same on `#screen-board`;
   - `#thread-bar` becomes one line of about 44 px (hide `#thread-cwd`, `#thread-model`, title ellipsised);
   - hide `.rp-composer` and `#composer`;
   - the terminal takes the freed height.
   Keep `.rp-ask`/`.rp-asks`, `#secret-row`, `#outbox-note`, `#term-note` and `#term-new-output` visible: they are interrupts or answers, not chrome.
3. **Term bar tucked** (`app/index.html`, `app/app.js`, `app/style.css`). Add a `⋯` button to `#thread-bar` that toggles `#term-bar` (A−/A+, Keyboard, Take over, Watching chip) in read mode; it starts hidden. Show `#term-bar` regardless while driving (Take over or Keyboard is on, so Hand back is reachable) and while `#term-note` has text (a refused send must still say why, #TBR2).
4. **Floating prompt button** (`app/index.html`, `app/style.css`). Add a `#compose-fab` button: round, 52 px, bottom right, inset by `env(safe-area-inset-*)`, accent colour, labelled "Write a prompt". It is shown only in read mode and only when a composer would be shown (the same `canCompose` rule `updateDriveUi()` uses, ~L1258), and hidden while direct keys drive the terminal. It carries a dot while the box holds an unsent draft.
5. **Write mode** (`app/app.js`, `app/style.css`, `app/pane.css`). The button sets `data-mode=write`, which:
   - hides `#terminal-pane`, `.rp-thinking` and `.rp-queue`;
   - keeps the header (the `‹` becomes an `×` labelled "Close") and `.rp-ask` (the question being answered is the context for the typing);
   - shows the composer growing into the remaining height, with its textarea `max-height` lifted, and focuses the textarea (`.rp-input`, or `#composer-text` when no pane view is mounted).
   `×` closes it with the draft kept. A successful send closes it too (the prompt went; the owner wants to see the answer), via the pane view's send callback and `sendPrompt()`. The sheet sits on `--app-height`, so it rides above the keyboard on iOS.
6. **Back** (`app/app.js`). In `closeOneLayer()`, after the pane-view sheet and before `closePane()`, add `if (writeMode) { leaveWriteMode(); return true; }`. Call `syncBackEntry()` after entering write mode so a spare history entry exists (`anythingOpen()` is already true on a pane, so the entry is there; check it).
7. **Word wrap on the phone** (`app/screen.js`, `app/style.css`). Add a `wrap` option. When it is on, the screen is not `alt` (TUIs keep their grid) and the view is phone width: rows get `white-space: pre-wrap; overflow-wrap: anywhere`, `fit()` keeps the reader's floor font and does not size for `cols`, and sideways cursor-following is skipped. Toggle it from a "Wrap" button in the tucked `#term-bar` (default on for phones), stored like the font floor. Check that `linkify`'s wrapped-URL rule still holds (it works per desktop row, which wrapping does not change).
8. **Resize and rotation.** Re-evaluate `phoneLayout()` on the same resize path `refit()` uses: rotating to a wide landscape removes `data-mode`, and write mode's draft survives.
9. **Tests** (`tests/test_remote_browser.py`, plus `tests/test_web_screen.py` for wrap):
   - a phone test at 390×844 for read mode: `#bar` not displayed with the link ok; `#thread-bar` ≤ 48 px tall; composer not displayed; FAB inside the bottom-right quarter; the terminal's bottom within 8 px of the viewport's bottom;
   - tap FAB: `#terminal-pane` not displayed, textarea focused;
   - type then ×: back in read mode, the draft kept, the FAB dot on;
   - FAB then `history.back()`: write mode closes and the pane stays open;
   - send: the prompt reaches the demo desktop and read mode is back;
   - at 820×1180 nothing changed (composer docked, no FAB);
   - wrap: an 80-col row of text at 390 px has `grid.scrollWidth <= clientWidth` and more than one line box, and an `alt` screen does not wrap.
   Update the existing phone tests that type straight into the docked box (e.g. `test_pair_and_drive_a_pane_from_the_browser`, the outbox tests) to open write mode first. Screenshots via `RELAY_SHELL_SHOTS` into `docs/qa_evidence/<date>-phone-read-write/`.
10. **Docs.** Add a short paragraph to the phone section of `docs/REMOTE-PROTOCOL.md` or the app's doc (whichever describes screens) naming the two modes. There is no protocol change: this is client-only.

**Out of scope / decisions for the owner.**
- **Guests** (`app/guest.js`, `#screen-guest`) keep their current layout. A guest's box is editor-only and already a single row. Same treatment is a follow-up if wanted.
- **Send closes write mode.** Recommended because the owner wants "to see what it says"; the alternative is manual × only. Say if you want the sheet to stay open after sending.

**Risks.**
- iOS Safari's keyboard is not reproducible in Chrome emulation. `--app-height` from `viewport.js` is the mechanism, and the owner's check on a real iPhone is the `person` part of the verify block.
- Wrapping changes row heights, so the scrollback prefetch and "New output" paths must be rechecked with wrap on (existing tests in `test_remote_browser.py` for scrollback cover this if run at the phone size).
- `.rp-*` rules come from `pane.css`, whose specificity is high (`.relay-pane …`). Mode selectors must be scoped `#screen-thread[data-mode=…] .relay-pane …` to win.

**Verify.** Run the new and updated phone tests in `tests/test_remote_browser.py` and `tests/test_web_screen.py`, then `tests/test_pane_view.py` and `tests/test_web_viewport.py` (unchanged behaviour at wider widths and with the keyboard up). Check the screenshots at 390×844 in read mode, write mode and with wrap on. The owner then opens a pane on the iPhone.
