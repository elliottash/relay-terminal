<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# The phone app, reviewed and driven — 2026-09-22

A review of the recent phone work (#PBXC, #SWPH and the four commits `7f945e42`, `f919f14b`,
`b8222865`, `88ba42ab`) followed by a live drive of the web client at 390×844 in a real headless
Chrome, touch emulation on. Nothing here is a test: it is a drive that leaves pictures, numbers
and a log, and every finding below was reproduced in the browser or traced in the code.

## The benches

| Bench | What it is | Files |
|---|---|---|
| A | the pane view (`app/pane.js`) on the `tests/fixtures/pane_state/` fixtures through `app/pane-demo.html` | `pane_drive.py`, `A-*.png`, `D-*.png`, `E-*.png` |
| B | the whole client against a **real** rendezvous, host and Noise link (`tests/test_remote_browser.py`'s `Harness`), with a terminal whose rows carry URLs, a bracketed URL, a `www.` one, a path and a line wider than the screen | `app_drive.py`, `B-*.png`, `C-*.png` |
| C | `app/screen.js` on its own bench (`tests/screen_harness.html`) for the sideways scroll | `F-*.png` |
| D | the phone Board (`tests/test_board_view.py`, `RELAY_BOARD_SHOTS=`) | `board/` |

```sh
python3 docs/qa_evidence/2026-09-22-phone-ux-drive/pane_drive.py
python3 docs/qa_evidence/2026-09-22-phone-ux-drive/app_drive.py
RELAY_BOARD_SHOTS=docs/qa_evidence/2026-09-22-phone-ux-drive/board \
  python3 -m unittest tests.test_board_view          # 26 tests, all pass at 88ba42ab
```

## What is right

* **The link painter (`88ba42ab`) behaved on every row it was given.** `B-02-terminal.png` and the
  log: a URL in a sentence stops at the full stop, `…/pull/12#issue-7,` drops the comma and keeps
  the fragment, `(see https://example.com/a_(b)_c)` keeps its inner brackets and loses the outer
  one, `www.example.com` gains `https://`, and `mailto:` and `ftp://` are left alone. Every anchor
  is `target=_blank rel="noopener noreferrer"`.
* **The sideways scroll survives output** (`F-hscroll-long-lines.png`): 80 columns of real text at
  the 12 px floor make the grid 617 px wide on a 390 px screen; scrolled to `scrollLeft: 227`, six
  more frames of output leave it at 227. The grid is only as wide as its widest line, so short
  output does not scroll sideways at all.
* **The font floor and A−/A+ persist.** 12 px at rest, 20 px after four A+, and the reader's choice
  survives a reload (`relay.term-font-floor`).
* **The old client composer does bring the keyboard down on send** (`focus` goes to `BODY`).
  The pane view's own Send button does not — finding 3.
* **The phone Board** passes all 26 of its browser tests at HEAD, in portrait, landscape-with-
  keyboard and on an iPad (`board/`).

## What is wrong

Each was reproduced at 390×844 unless it says otherwise.

### 1. The effort chip never follows a level-only `pane_state` — `app/pane.js:1042`, `:1058`

`renderEffort()` is called from the last line of `renderModel()`, which returns early when the
model's signature (label + choices — the level is in neither) has not moved. So every
`pane_state` in which only the level changed is dropped.

```
start              {"value":"high","shown":"✓ high","opts":["low","medium","✓ high"]}
after effort=low   {"value":"high","shown":"✓ high","opts":["low","medium","✓ high"]}   <-- ignored
after model change {"value":"low", "shown":"✓ low", "opts":["✓ low","medium","high"]}   <-- only now
```

The level moving at the desktop, another partner's `effort_pick`, and the echo of the phone's own
pick are all invisible until something about the model happens to change.

### 2. The chip reads `✓▾high` — `app/pane.js:297`, `app/pane.css:425-435`

`D-effort-chip-closeup.png`. Two faults in one chip:

* the `✓ ` that marks the current option is baked into that option's *text*, so it shows in the
  **closed** control. The model select next to it avoids this with a disabled placeholder option
  carrying the plain label, and reads `fake · local`.
* `.rp-model-chevron` is `position: absolute; right: 7px` inside `.rp-model-wrap`, and the wrap now
  holds `model, chevron, effort`. The chevron is therefore anchored to the **effort** select's
  right edge and paints over its text, while the model keeps a 22 px empty gutter where its own
  chevron used to be. Measured: chevron at x 216.4–223.6, effort at x 198–289.

### 3. The pane view's Send button puts the keyboard back up — `app/pane.js:1114-1118`

```js
on(sendButton, 'click', () => {
  const text = box.value;
  if (text.trim()) compose(text, busy() ? 'queue' : 'now');
  box.focus();                    // undoes the box.blur() compose() just did
});
```

Measured: focus after tapping Send is still `rp-input`; after Enter it is the body. The tap is the
gesture a phone actually uses, so `f919f14b`'s "the keyboard comes down on send" does not hold on
the surface a real desktop mounts.

### 4. Every Copy id outcome is painted under the sheet — `app/pane.js:1309/1317/1337`, `app/pane.css:477`

The toast lives in `.rp-term-wrap` (no z-index); the sheet layer is `z-index: 10` over `inset: 0`
with a 70 %-opaque backdrop, and Copy id is the one sheet control that does **not** close the
sheet first. Measured with `elementFromPoint` at the toast's centre: the topmost element is
`rp-session-when`, a row of the sheet. Success, "Clipboard refused — long-press to copy" and
"The id was refused: …" are all equally invisible, so the button is indistinguishable from dead.

### 5. The clipboard write happens after a round trip — `app/pane.js:655-661` → `:1303-1320`

The tap only sends `conversation_id`; `navigator.clipboard.writeText()` runs later, in the answer
handler, with no user activation left. `app/app.js:802` already records the rule for this codebase
— *"First thing in the gesture: Safari drops the user activation across an await."* In this drive's
headless Chrome the write **was** refused and the fallback ran, which is the iOS path.

The fallback then appends the id to `document.querySelector('.rp-sheet')` — a document-global,
kind-blind query against an element `closeSheet()` leaves in the DOM — so with the sheet dismissed
the id is appended to a hidden layer, and with another sheet open it lands in that one.

### 6. The Conversations sheet throws the reader back to the top every minute — `app/pane.js:1093-1097`

The rebuild is gated on a signature of the whole `sessions` block, which includes each row's
`when` — a relative time the desktop recomputes on every publish and which steps at one-minute
granularity. Measured on the 50-row fixture: scrolled to `scrollTop: 2010`, one `when` tick →
`scrollTop: 0`, focus back on "New conversation", and the finding-5 fallback id deleted with it.

### 7. The terminal bar's sentence is clipped on a phone — `app/index.html:183-190`

`C-full-after-send.png`: "Agent runnin…". Adding A−/A+ to that row left `#term-note` 91 px between
the "Watching" chip and the buttons at `full` (`scrollWidth 178`, `clientWidth 91`), 195 px at
`agent`. Every sentence the client writes there is cut — including the *failure* of a send, which
`sendPrompt`'s `fail()` puts in exactly this label.

### 8. A device paired `agent` is told it may only watch — `app/app.js:1113`

`allowed = capability === 'full'`, so anything below `full` gets "This device is paired for viewing
only." An `agent` device has a working composer on screen and sent a prompt in this drive
(`C-agent-after-send.png` — the note says viewing-only above a live Send button).

### 9. Nothing listens for the back button — `app/*.js`

`grep -n "popstate\|pushState\|history.back" app/*.js` is empty; the only history calls are
`replaceState`. On Android the system Back closes the installed app instead of closing the sheet or
leaving the thread, and in a tab it leaves the origin, taking the connection and the in-memory
outbox with it.

### 10. A reconnect never asks for the pane's state — `app/app.js:504`

`openPane()` sends `pane_focus` **and** `pane_state_get` (`app/app.js:977-978`); the reconnect path
sends only `pane_focus`. A phone that slept through the end of a turn comes back with the old
queue, the old Stop and the old strip until something else moves.

## Provenance

`main` at `88ba42ab`, Ubuntu 24.04 aarch64, Chrome at `/usr/bin/google-chrome`, viewport 390×844,
DPR 2–3, touch emulation on. Benches A and C need no desktop; bench B runs the real
`rendezvous.server`, `remote/host.py` and a Noise session on loopback. Nothing of the owner's
profile is touched: the harness makes its own `tempfile` identity and store.
