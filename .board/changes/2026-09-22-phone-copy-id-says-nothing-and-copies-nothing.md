---
id: CPY4
type: work
status: needs-verification
labels: [bug, remote]
assignee: claude-code
implemented_by: anthropic/claude-opus-5 via claude-code
rank: zcpy4
created: '2026-09-22'
source: Measured by Claude Code driving the phone app at 390x844, 2026-09-22
links: {plans: [], commits: [7f945e42, 782808f9, 7d313776], evidence: [docs/qa_evidence/2026-09-22-phone-ux-drive/, docs/qa_evidence/2026-09-22-streamA-pane/], related: [PH0N], github: null}
---
# Copy id on the phone says nothing, copies nothing on iOS, and the sheet jumps every minute

## Issue
Four faults around the Conversations sheet, reproduced at 390×844
(`docs/qa_evidence/2026-09-22-phone-ux-drive/`, findings 4–6).

**1. Every outcome is painted under the sheet.** The toast lives in `.rp-term-wrap`, which has no
z-index (`app/pane.js:227`, `:350`); the sheet layer is `z-index: 10` over `inset: 0` with a
70 %-opaque backdrop (`app/pane.css:477`). Copy id is the one sheet control that does **not** call
`closeSheet()` first, so the sheet is guaranteed to still be up when the answer lands. Measured
with `elementFromPoint` at the toast's centre: the topmost element is `rp-session-when`, a row of
the sheet. "Conversation id … copied", "Clipboard refused — long-press to copy" and "The id was
refused: …" are all equally invisible, so the button is indistinguishable from dead.
`tests/test_pane_view.py:490` reads the toast's `textContent`, which passes on an invisible toast.

**2. The clipboard write is outside the user gesture.** The tap only sends `conversation_id`
(`app/pane.js:655-661`); `navigator.clipboard.writeText()` runs later in the answer handler
(`:1303-1320`), after a wire round trip. This codebase already records the rule at
`app/app.js:802` — "First thing in the gesture: Safari drops the user activation across an await."
In this drive's headless Chrome the write **was** refused and the fallback ran, which is the iOS
path. The shape that works is the reverse: fetch the id when the sheet opens (or on the previous
gesture) and let the tap do a synchronous write.

**3. The fallback lands nowhere.** It appends the id to `document.querySelector('.rp-sheet')` — a
document-global, kind-blind query against an element `closeSheet()` leaves in the DOM (it only
hides the layer and clears the text). With the sheet dismissed the id is appended to a hidden
layer; with the row-actions or send-when sheet open it is appended to that one, under
"Send now / Queue / Steer".

**4. The sheet throws the reader back to the top every minute.** The rebuild is gated on a
signature of the whole `sessions` block (`app/pane.js:1093-1097`), which includes each row's
`when` — a relative time the desktop recomputes on every publish and which steps at one-minute
granularity. Measured on the 50-row fixture: scrolled to `scrollTop: 2010`, one `when` tick →
`scrollTop: 0`, focus back on "New conversation", and fault 3's fallback line deleted with it.
While a turn runs `pane_state` arrives about ten times a second, which is what the guard above it
was added for; the guard just does not exclude the clock.

Also on this path: `conversation_id` is not in the outbox's `QUEUEABLE` and the view's `send` passes
no `onError` (`app/app.js:926`), so tapping Copy id while the link is down is swallowed silently
and leaves `idRequest` set, waiting for an answer that was never asked for.

## Done means
Copy id says what happened where the reader is looking: the sheet closes on the tap and the toast
is on top of whatever is drawn, so success, a refusal and a failure are each legible. On iOS the id
reaches the clipboard, which means the write happens inside the tap rather than after a round trip.
Where the clipboard refuses, the id is left on screen as selectable text the reader can still copy
by hand — in the toast, since the sheet is closed by then. The conversations list holds its scroll
while a turn runs. It fails if `elementFromPoint` at the toast's centre returns anything but the
toast, or if a minute tick moves the list's `scrollTop`.

*(Corrected 2026-09-22 by the session that wrote it: the original asked both that the sheet close
on the tap and that the fallback attach to "the sheet that asked", which cannot both hold. The
original wording is in the thread.)*

## Execution Summary
**1.** The toast is `z-index: 20`, above the sheet layer's `10`, and Copy id — the one sheet
control that did not — closes the sheet on the tap.

**2.** The clipboard write left the answer handler. The id is asked for on the `pointerdown` that
precedes the tap and kept, so the tap writes it synchronously from what the press already brought
back; the wire is unchanged (`conversation_id` out, `conversation_id_text` back). Nothing
prefetches a sheet of ids: the hub allows 20 `conversation_id` a minute
(`remote/host.py:3191`) and 16 open asks (`ASKS_MAX`), so one press is one ask and an id already
answered for is never asked for twice. A tap that outruns its answer, and a keyboard's Enter (which
has no press), still write when the answer lands.

**3.** The document-global `document.querySelector('.rp-sheet')` fallback is gone: a refused id goes
into the toast itself as selectable `.rp-session-id` text, held 20 s instead of 4.

**4.** The sessions rebuild is gated on what the sheet is actually built from — `can_new`,
`can_open`, and each row's id, title and `current` — while the clock and the running dot are
patched into the existing nodes. Rows keep their identity, the scroll holds, and the clock still
moves.

**Where this departs from `## Done means`, and why.** That section asked both that the sheet close
on the tap *and* that the fallback text attach to "the sheet that asked". Those cannot both hold:
with the sheet closed there is no sheet to attach to. The fallback is therefore in the toast —
which, after fix 1, is the topmost thing on the screen and is selectable, so it does the job the
clause was written for. The `## Done means` has been corrected to say so; the original wording is
in this card's thread.

**Not done, and why.** The card's last paragraph — `conversation_id` missing from the outbox's
`QUEUEABLE` and the view's `send` passing no `onError`, so a tap while the link is down is
swallowed and leaves `idRequest` set — is `app/app.js:926`, which belongs to stream D. It is not
fixed here and is not claimed below.

## Tests
`RELAY_KEYRING=off python3 -m unittest tests.test_pane_view` — 36 tests, OK, 16 s. Re-run by the
orchestrating session after the landing.

- `tests/test_pane_view.py::PaneViewTests::test_the_press_asks_for_the_id_and_the_tap_writes_it`
- `tests/test_pane_view.py::PaneViewTests::test_a_refused_id_is_readable_over_an_open_sheet`
- `tests/test_pane_view.py::PaneViewTests::test_the_conversations_list_holds_its_scroll_across_a_clock_tick`
- `manual: docs/qa_evidence/2026-09-22-streamA-pane/` — `CPY4-toast-over-the-sheet.png`,
  `CPY4-refused-id-over-an-open-sheet.png`, `CPY4-list-holds-its-scroll.png`; clipboard `""` before
  the tap and the id after it.
- `manual: docs/qa_evidence/2026-09-22-phone-ux-drive/` — the two probes this card was filed from,
  re-run unchanged against the landed tree by the orchestrating session:
  * toast z-order — `{"topmost":"rp-session-id","isToastOrChild":true,"zIndex":"20"}`, where it was
    `{"topmost":"rp-session-when","isToastOrChild":false,"zIndex":"auto"}`; the refused id is in
    the toast's own text.
  * the list across a clock tick — `scrollTop 1975 → 1975` with the same row nodes, where it was
    `2010 → 0`. A row whose **title** changed still rebuilds (`scrollTop → 0`, new nodes), so the
    guard has not been widened into never repainting.

**Not proven, and not claimed:** Copy id while the link is down (the card's last paragraph). That
is `app/app.js`, stream D's file.
