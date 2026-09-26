---
id: 8R3V
type: work
status: discussing
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-25'
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
