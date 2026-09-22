<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# The phone client's shell: the terminal bar, and coming back — #TBR2 and #PKT5

2026-09-22. The fixes for two cards out of `docs/qa_evidence/2026-09-22-phone-ux-drive`
(findings 7, 8, 9 and 10), and what was measured to show they hold. Every screenshot here is a
real headless Chrome at 390×844, paired over a real Noise session with a real host and
rendezvous on loopback — the same harness the drive used, driven from
`tests/test_remote_browser.py`.

```
RELAY_SHELL_SHOTS=docs/qa_evidence/2026-09-22-streamD-shell \
  python3 -m unittest tests.test_remote_browser.TerminalBarTests \
                      tests.test_remote_browser.PocketAndBackTests
```

Six tests, about nine seconds. Without `RELAY_SHELL_SHOTS` they run and take no pictures.

## #TBR2 — the terminal bar

`#term-note` shared its row with the "Watching" chip, A−/A+ and "Take over". The drive measured
91 px of room at `full` and 195 px at `agent`, and every sentence was cut mid-word — the reason a
send did not go included, since that label is where it is reported and nowhere else.

The note is now last in the row and a full line wide, so it wraps under the controls, and it is
`hidden` while empty so the line only exists when it says something. Every writer goes through
one `termNote()` in `app/app.js`.

| screenshot | what it shows |
|---|---|
| `term-bar-agent.png` | an `agent` device: "You can ask the agent here. Taking the keyboard needs full access." — whole, on two lines, above the composer the desktop answers |
| `term-bar-agent-model-change.png` | the longest sentence the client can write there (`modelLine`, a `model_changed` that applies after compaction), whole |
| `term-bar-full-agent-running.png` | `full`, mid-turn: "Agent running…" — the 91 px case from the drive |
| `term-bar-full-model-change.png` | the same long sentence at `full`, beside "Take over" |
| `term-bar-longest-sentence.png` | every fixed sentence app.js can write, measured one after another in the real bar |

Measured, not eyeballed: `test_…_reads_the_whole_sentence` asserts
`scrollWidth === clientWidth` and `scrollHeight === clientHeight` on `#term-note`, that it sits
below the A+ button rather than beside it, and that it stays inside 390 px. Put the old rule
back — `flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap` — and all three
tests fail at the drive's own number:

```
AssertionError: 99 != 91 : written: Agent running……: 'Agent running…' is cut off
                           (99 px of sentence in 91 px)
```

The sentences are not a list somebody has to keep up to date: `fixed_sentences()` sweeps
`termNote('…')`, `note('…')` and `voiceNote('…')` out of `app/app.js`, so a sentence added
tomorrow is measured tomorrow. The four the client *builds* rather than spells out — the
microphone refusal, the holder line with a name from the wire in it, a model change, and the
desktop's own refusal — are listed beside it in `BUILT_SENTENCES`.

**And an `agent` device is no longer told it may only watch.** `updateDriveUi()` wrote "This
device is paired for viewing only." for everything below `full`, while `canCompose` is
`agent || full` two lines later — so the drive typed a prompt into a working composer under a
sentence saying it could not. `agent` now gets its own words, and only `view` is told it is
watching. The test asserts the sentence *and* that `#composer` is really on screen, which is what
made the old one wrong rather than merely terse.

## #PKT5 — coming back from a pocket, and the back button

Items 1, 2 and 3. Item 4 (the ask buttons and queue rows rebuilt ten times a second) is in
`app/pane.js` and belongs to another session.

### 1. A resume asks for the pane's state — `resume-pane-state.png`

`pane_focus` replays the agent ring, a screen snapshot, `control` and `participants`. It does not
replay a `pane_state`, and `resume` deliberately cannot ("the latest state is kept outside
`self.streams` on purpose", `remote/host.py`). An idle pane publishes on change only, and the
change happens while the phone is asleep — so the pane a phone came back to was the pane it left.

`openPane` and the reconnect now go through one `refocusPane()`, which sends `pane_focus` and,
under the same `features.includes('pane_state')` guard, `pane_state_get`.

The test drops the channel from the host's side, publishes a state while the socket is down (the
running prompt finishes, two more queue), waits for the client's own reconnect, and asserts the
phone shows three rows and no running prompt. Without the `pane_state_get` it times out on one
row: nothing else ever delivers that state.

### 2. The system Back button — `back-to-the-inbox.png`

`grep -n "popstate\|pushState\|history.back" app/*.js` was empty. On Android, Back closed the
installed app from a thread; in a tab it left the origin, taking the connection, the pane list and
the in-memory outbox with it.

While anything is open the app keeps one spare history entry. Back pops that instead of the page,
the deepest open layer closes, and a new spare goes on while anything is still open. With nothing
open the spare is dropped, so Back from the inbox leaves in one press, as it should.

The test marks the document (`window.__thisDocument`), presses Back from a thread, and asserts the
inbox is up, the mark is still there, the URL has not moved, the link still says `connected`, and
exactly one screen is drawn — then presses Back again and watches the app leave for `about:blank`,
so Back is closed, not trapped.

**What is not done here.** Back unwinds a sheet through the view's own handle —
`paneView.closeSheet()` and `board.closeSheet()` / `board.closeCard()` — and neither
`mountPane` (`app/pane.js`) nor `mountBoard` (`app/board.js`) returns one today. Both files
belong to other sessions, so the call is written and falls through: Back from a card closes the
board to the inbox rather than returning to the card list, and Back with a pane-view sheet open
closes the pane under it. Each needs one line on the other file's returned object
(`closeSheet()` returning whether it closed something).

### 3. A notification onto another pane — `notification-onto-another-pane.png`

The `open_card` branch of the service-worker handler closed the open pane first; `open_pane` and
`requestOpenPane` did not, and `openPane` never called `closePaneView()`. So pane A's mounted
view — and any sheet up in it — stayed on screen under pane B's title, and a row tapped in it
emitted `conversation_open` for A. `ensurePaneView()`'s early return also skipped the
`openQuestions` hand-off, so B's ask, replayed on `pane_focus` before its first `pane_state`, was
never drawn.

`openPane` now releases the pane it is leaving through the same `leavePane()` `closePane` uses:
the keyboard if this device holds it, `pane_blur`, and the mounted view. The screenshot is the
end of the test — the agent on pane-2 asked "Drop the old table first?" while the phone was
looking at pane-1, a notification for pane-2 was tapped, and pane-2's view is up with its own two
queue rows and the ask that had been waiting for it. Before the fix the same test finds pane-1's
three rows still on screen under "engine tests".
